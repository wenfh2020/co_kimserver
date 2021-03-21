#include "mysql_mgr.h"

#include "error.h"

#define DEF_CONN_CNT 5
#define MAX_CONN_CNT 100
#define TASK_QUEUE_LIMIT 1000
#define CONN_TIME_OUT (10 * 1000)

namespace kim {

MysqlMgr::MysqlMgr(Log* logger) : Logger(logger) {
}

MysqlMgr::~MysqlMgr() {
    destory();
}

int MysqlMgr::sql_write(const std::string& node, const std::string& sql) {
    if (node.empty() || sql.empty()) {
        LOG_ERROR("invalid db exec params!");
        return ERR_INVALID_PARAMS;
    }
    return send_task(node, sql, false);
}

int MysqlMgr::sql_read(const std::string& node, const std::string& sql, vec_row_t& rows) {
    if (node.empty() || sql.empty()) {
        LOG_ERROR("invalid db query params!");
        return ERR_INVALID_PARAMS;
    }
    return send_task(node, sql, true, &rows);
}

int MysqlMgr::send_task(const std::string& node, const std::string& sql, bool is_read, vec_row_t* rows) {
    LOG_DEBUG("send mysql task, node: %s, sql: %s.", node.c_str(), sql.c_str());

    int ret;
    task_t* task;
    co_data_t* cd;

    for (int i = 0; i < 10; i++) {
        cd = get_co_data(node);
        if (cd == nullptr) {
            LOG_ERROR("can not find conn, node: %s", node.c_str());
            return ERR_DB_TASKS_OVER_LIMIT;
        }
        if (cd->tasks.size() > TASK_QUEUE_LIMIT) {
            co_sleep(30);
            continue;
        }
        break;
    }

    if (cd->tasks.size() > TASK_QUEUE_LIMIT) {
        LOG_WARN("db task over limit! node: %s.", node.c_str());
        return ERR_DB_TASKS_OVER_LIMIT;
    }

    task = new task_t;
    task->co = GetCurrThreadCo();
    task->is_read = is_read;
    task->sql = sql;
    task->query_res_rows = rows;

    cd->tasks.push(task);

    co_cond_signal(cd->cond);
    LOG_TRACE("signal mysql co handler! node: %s, co: %p", node.c_str(), cd->co);
    co_yield_ct();

    ret = task->ret;
    SAFE_DELETE(task);
    return ret;
}

void* MysqlMgr::co_handle_task(void* arg) {
    co_enable_hook_sys();
    co_data_t* cd = (co_data_t*)arg;
    MysqlMgr* m = (MysqlMgr*)cd->privdata;
    return m->handle_task(arg);
}

void* MysqlMgr::handle_task(void* arg) {
    task_t* task;
    uint64_t begin, spend;
    co_data_t* cd = (co_data_t*)arg;

    for (;;) {
        if (cd->tasks.empty()) {
            LOG_TRACE("no redis task, pls wait! node: %s, co: %p",
                      cd->db->node.c_str(), cd->co);
            co_cond_timedwait(cd->cond, -1);
            continue;
        }

        if (cd->c == nullptr) {
            cd->c = new MysqlConn(logger());
            if (!cd->c->connect(cd->db)) {
                LOG_ERROR("db connect failed! node: %s, host: %s, port: %d",
                          cd->db->node.c_str(), cd->db->host.c_str(), cd->db->port);
                clear_co_tasks(cd);
                SAFE_DELETE(cd->c);
                co_sleep(1000);
                continue;
            }
        }

        task = cd->tasks.front();
        cd->tasks.pop();

        begin = mstime();
        cd->is_working = true;
        cd->active_time = begin;
        m_cur_handle_cnt++;

        if (task->is_read) {
            task->ret = cd->c->sql_read(task->sql, *task->query_res_rows);
        } else {
            task->ret = cd->c->sql_write(task->sql);
        }

        cd->is_working = false;
        spend = mstime() - begin;
        if (spend > m_slowlog_log_slower_than) {
            LOG_WARN("slowlog - sql spend time: %llu, sql: %s", mstime() - begin, task->sql.c_str());
        }

        co_resume(task->co);
    }

    return 0;
}

MysqlMgr::co_data_t* MysqlMgr::get_co_data(const std::string& node) {
    co_data_t* cd;
    co_array_data_t* ad;

    auto it = m_dbs.find(node);
    if (it == m_dbs.end()) {
        LOG_ERROR("can not find node: %s.", node.c_str());
        return nullptr;
    }

    auto itr = m_coroutines.find(node);
    if (itr == m_coroutines.end()) {
        ad = new co_array_data_t;
        ad->db = it->second;
        m_coroutines[node] = ad;
    } else {
        ad = (co_array_data_t*)itr->second;
        if ((int)ad->coroutines.size() >= ad->db->max_conn_cnt) {
            cd = ad->coroutines[ad->cur_co_idx % ad->coroutines.size()];
            if (++ad->cur_co_idx == (int)ad->coroutines.size()) {
                ad->cur_co_idx = 0;
            }
            return cd;
        }
    }

    cd = new co_data_t;
    cd->db = it->second;
    cd->privdata = this;
    cd->cond = co_cond_alloc();
    ad->coroutines.push_back(cd);

    LOG_INFO("node: %s, co cnt: %d, max conn cnt: %d",
             node.c_str(), (int)ad->coroutines.size(), ad->db->max_conn_cnt);

    co_create(&(cd->co), nullptr, co_handle_task, cd);
    co_resume(cd->co);
    return cd;
}

void MysqlMgr::on_repeat_timer() {
    int conn_cnt = 0;
    int task_cnt = 0;

    run_with_period(1000) {
        for (auto& it : m_coroutines) {
            co_array_data_t* ad = it.second;
            for (auto& v : ad->coroutines) {
                task_cnt += v->tasks.size();
                if (v->c != nullptr) {
                    conn_cnt++;
                }
            }
        }

        if (task_cnt > 0 || m_old_handle_cnt != m_cur_handle_cnt) {
            LOG_DEBUG("conn cnt: %d, avg handle cnt: %d, waiting cnt: %d",
                      conn_cnt, m_cur_handle_cnt - m_old_handle_cnt, task_cnt);
            m_old_handle_cnt = m_cur_handle_cnt;
        }

        /* 根据速率去调整。 */
        /* recover connections. no waiting task, not in working, and timeout. */
        for (auto& it : m_coroutines) {
            co_array_data_t* ad = it.second;
            auto itr = ad->coroutines.begin();
            for (; itr != ad->coroutines.end(); itr++) {
                co_data_t* v = *itr;
                if (v->c != nullptr && v->tasks.empty() &&
                    !v->is_working && mstime() > v->active_time + CONN_TIME_OUT) {
                    LOG_INFO("recover db coroutines, ad cnt: %lu, node: %s, co: %p",
                             ad->coroutines.size(), v->db->node.c_str(), v->co);
                    v->c->close();
                    SAFE_DELETE(v->c);
                }
            }
        }
    }
}

bool MysqlMgr::init(CJsonObject* config) {
    if (config == nullptr) {
        LOG_ERROR("invalid params!");
        return false;
    }

    db_info_t* db;
    std::vector<std::string> vec;

    m_slowlog_log_slower_than = str_to_int((*config)("slowlog_log_slower_than"));

    (*config)["nodes"].GetKeys(vec);
    if (vec.size() == 0) {
        LOG_ERROR("database info is empty.");
        return false;
    }

    for (const auto& v : vec) {
        const CJsonObject& obj = (*config)["nodes"][v];

        db = new db_info_t;
        db->host = obj("host");
        db->db_name = obj("name").empty() ? "mysql" : obj("name");
        db->password = obj("password");
        db->charset = obj("charset");
        db->user = obj("user");
        db->port = str_to_int(obj("port"));
        db->max_conn_cnt = str_to_int(obj("max_conn_cnt"));
        db->node = v;

        if (db->max_conn_cnt == 0) {
            db->max_conn_cnt = DEF_CONN_CNT;
        } else {
            if (db->max_conn_cnt > MAX_CONN_CNT) {
                LOG_WARN("db max conn count is too large! cnt: %d", db->max_conn_cnt);
                db->max_conn_cnt = MAX_CONN_CNT;
            }
        }

        LOG_DEBUG("max client cnt: %d", db->max_conn_cnt);

        if (db->host.empty() || db->port == 0 ||
            db->password.empty() || db->charset.empty() || db->user.empty()) {
            LOG_ERROR("invalid db node info: %s", v.c_str());
            SAFE_DELETE(db);
            destory();
            return false;
        }

        m_dbs.insert({v, db});
    }

    return true;
}

void MysqlMgr::destory() {
    for (auto& it : m_dbs) {
        SAFE_DELETE(it.second);
    }
    m_dbs.clear();

    for (auto& it : m_coroutines) {
        co_array_data_t* d = it.second;
        for (auto& v : d->coroutines) {
            SAFE_DELETE(v->c);
            co_release(v->co);
            co_cond_free(v->cond);
            while (!v->tasks.empty()) {
                SAFE_DELETE(v->tasks.front());
                v->tasks.pop();
            }
        }
    }
    m_coroutines.clear();
}

void MysqlMgr::clear_co_tasks(co_data_t* cd) {
    task_t* task;

    while (!cd->tasks.empty()) {
        task = cd->tasks.front();
        cd->tasks.pop();
        co_resume(task->co);
    }
}

}  // namespace kim