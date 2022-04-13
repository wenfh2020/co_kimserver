#include "mysql_mgr.h"

#include "error.h"

#define DEF_CONN_CNT 5
#define MAX_CONN_CNT 100
#define TASK_TIME_OUT (10 * 1000)
#define CONN_TIME_OUT (30 * 1000)
/* distribute tasks to connection one time. */
#define DIST_CONN_TASKS_ONCE 50

namespace kim {

MysqlMgr::MysqlMgr(std::shared_ptr<Log> logger) : Logger(logger) {
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

    auto md = get_co_mgr_data(node);
    if (md == nullptr) {
        LOG_ERROR("invalid node: %s", node.c_str());
        return ERR_CAN_NOT_FIND_NODE;
    }

    auto task = new task_t;
    task->sql = sql;
    task->rows = rows;
    task->is_read = is_read;
    task->user_co = co_self();
    task->active_time = mstime();

    md->tasks.push(task);

    co_cond_signal(md->dist_cond);
    LOG_TRACE("signal task to dist coroutines! node: %s, co: %p",
              node.c_str(), md->dist_co);
    co_yield_ct();

    auto ret = task->ret;
    SAFE_DELETE(task);
    return ret;
}

void* MysqlMgr::on_dist_task(void* arg) {
    co_enable_hook_sys();

    co_mgr_data_t* md = (co_mgr_data_t*)arg;

    for (;;) {
        if (md->tasks.empty()) {
            LOG_TRACE("no task, pls wait! node: %s, co: %p",
                      md->db->node.c_str(), md->dist_co);
            co_cond_timedwait(md->dist_cond, -1);
            continue;
        }

        auto cd = get_co_data(md->db->node);
        if (cd == nullptr) {
            LOG_ERROR("can not get co data, node: %s", md->db->node.c_str());
            co_sleep(1000);
            continue;
        }

        /* distribute tasks to connection's coroutine. */
        for (int i = 0; i < DIST_CONN_TASKS_ONCE && !md->tasks.empty(); i++) {
            auto task = md->tasks.front();
            md->tasks.pop();
            cd->tasks.push(task);
        }

        co_cond_signal(cd->conn_cond);
    }

    return 0;
}

void* MysqlMgr::on_handle_task(void* arg) {
    co_enable_hook_sys();
    auto cd = (co_data_t*)arg;

    for (;;) {
        if (cd->tasks.empty()) {
            LOG_TRACE("no task, pls wait! node: %s, co: %p",
                      cd->db->node.c_str(), cd->conn_co);
            add_to_free_list(cd);
            co_cond_timedwait(cd->conn_cond, -1);
            continue;
        }

        if (cd->c == nullptr) {
            cd->c = new MysqlConn(logger());
            if (cd->c == nullptr || !cd->c->connect(cd->db)) {
                LOG_ERROR("db connect failed! node: %s, host: %s, port: %d",
                          cd->db->node.c_str(), cd->db->host.c_str(), cd->db->port);
                clear_co_tasks(cd, ERR_DB_CONNECT_FAILED);
                SAFE_DELETE(cd->c);
                co_sleep(1000);
                continue;
            }
        }

        auto task = cd->tasks.front();
        cd->tasks.pop();

        m_cur_handle_cnt++;
        cd->active_time = mstime();

        if (cd->active_time > task->active_time + TASK_TIME_OUT) {
            LOG_WARN("task time out, sql: %s.", task->sql.c_str());
            task->ret = ERR_DB_TASKS_TIME_OUT;
            co_resume(task->user_co);
            continue;
        }

        if (task->is_read) {
            task->ret = cd->c->sql_read(task->sql, *task->rows);
        } else {
            task->ret = cd->c->sql_write(task->sql);
        }

        auto spend = mstime() - cd->active_time;
        if (spend > m_slowlog_log_slower_than) {
            LOG_WARN("slowlog - sql spend time: %llu, sql: %s", spend, task->sql.c_str());
        }

        co_resume(task->user_co);
    }

    return 0;
}

MysqlMgr::co_mgr_data_t* MysqlMgr::get_co_mgr_data(const std::string& node) {
    auto it = m_coroutines.find(node);
    if (it != m_coroutines.end()) {
        return it->second;
    }

    auto it_db = m_dbs.find(node);
    if (it_db == m_dbs.end()) {
        LOG_ERROR("can not find db node: %s.", node.c_str());
        return nullptr;
    }

    auto md = new co_mgr_data_t;
    md->privdata = this;
    md->db = it_db->second;
    md->dist_cond = co_cond_alloc();

    m_coroutines[node] = md;
    co_create(
        &(md->dist_co), nullptr,
        [this](void* arg) { on_dist_task(arg); },
        md);
    co_resume(md->dist_co);
    return md;
}

MysqlMgr::co_data_t* MysqlMgr::get_co_data(const std::string& node) {
    co_data_t* cd;

    auto md = get_co_mgr_data(node);
    if (md == nullptr) {
        LOG_ERROR("can not find co array data. node: %s.", node.c_str());
        return nullptr;
    }

    /* get conn from free list. */
    if (!md->free_conns.empty()) {
        cd = md->free_conns.front();
        md->free_conns.pop_front();
        md->busy_conns.push_back(cd);
        return cd;
    }

    if ((int)md->conn_cnt == md->db->max_conn_cnt) {
        /* get conn from busy list. */
        cd = md->busy_conns.front();
        md->busy_conns.pop_front();
        md->busy_conns.push_back(cd);
        return cd;
    }

    cd = new co_data_t;
    cd->md = md;
    cd->privdata = this;
    cd->conn_cond = co_cond_alloc();
    cd->db = m_dbs.find(node)->second;

    md->conn_cnt++;
    md->busy_conns.push_back(cd);

    LOG_INFO("create new conn data, node: %s, co cnt: %d, max conn cnt: %d",
             node.c_str(), md->conn_cnt, md->db->max_conn_cnt);

    co_create(
        &(cd->conn_co), nullptr,
        [this](void* arg) { on_handle_task(arg); },
        cd);
    co_resume(cd->conn_co);
    return cd;
}

void MysqlMgr::on_repeat_timer() {
    co_enable_hook_sys();

    int task_cnt = 0;
    int busy_conn_cnt = 0;

    run_with_period(1000) {
        for (auto& it : m_coroutines) {
            co_mgr_data_t* md = it.second;
            for (auto& v : md->busy_conns) {
                task_cnt += v->tasks.size();
                if (v->c != nullptr) {
                    busy_conn_cnt++;
                }
            }
        }

        if (task_cnt > 0 || m_old_handle_cnt != m_cur_handle_cnt) {
            LOG_DEBUG("busy conn cnt: %d, avg handle cnt: %d, waiting cnt: %d",
                      busy_conn_cnt, m_cur_handle_cnt - m_old_handle_cnt, task_cnt);
            m_old_handle_cnt = m_cur_handle_cnt;
        }

        auto now = mstime();

        /* recover free connections. */
        for (auto& it : m_coroutines) {
            co_mgr_data_t* md = it.second;
            for (auto& v : md->free_conns) {
                if (v->c != nullptr && now > (v->active_time + CONN_TIME_OUT)) {
                    LOG_INFO("recover db conn, node: %s, coroutines cnt: %lu, co: %p, conn: %p",
                             v->db->node.c_str(), md->conn_cnt, v->conn_co, v->c);
                    v->c->close();
                    SAFE_DELETE(v->c);
                }
            }
        }
    }
}

void MysqlMgr::add_to_free_list(co_data_t* cd) {
    co_mgr_data_t* md = cd->md;

    auto it = md->busy_conns.begin();
    for (; it != md->busy_conns.end(); it++) {
        if (*it == cd) {
            md->busy_conns.erase(it);
            break;
        }
    }

    md->free_conns.push_front(cd);
}

void MysqlMgr::clear_co_tasks(co_data_t* cd, int ret) {
    task_t* task;

    while (!cd->tasks.empty()) {
        task = cd->tasks.front();
        cd->tasks.pop();
        task->ret = ret;
        co_resume(task->user_co);
    }
}

bool MysqlMgr::init(CJsonObject* config) {
    if (config == nullptr) {
        LOG_ERROR("invalid params!");
        return false;
    }

    std::vector<std::string> vec;
    (*config)["nodes"].GetKeys(vec);
    if (vec.size() == 0) {
        LOG_ERROR("database info is empty.");
        return false;
    }

    for (const auto& v : vec) {
        const CJsonObject& obj = (*config)["nodes"][v];

        auto db = new db_info_t;
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

    m_slowlog_log_slower_than = str_to_int((*config)("slowlog_log_slower_than"));
    return true;
}

void MysqlMgr::destory() {
    for (auto& it : m_dbs) {
        SAFE_DELETE(it.second);
    }
    m_dbs.clear();

    for (auto& it : m_coroutines) {
        co_mgr_data_t* md = it.second;
        co_release(md->dist_co);
        co_cond_free(md->dist_cond);
        while (!md->tasks.empty()) {
            SAFE_DELETE(md->tasks.front());
            md->tasks.pop();
        }

        for (auto& v : md->busy_conns) {
            SAFE_DELETE(v->c);
            co_release(v->conn_co);
            co_cond_free(v->conn_cond);
            while (!v->tasks.empty()) {
                SAFE_DELETE(v->tasks.front());
                v->tasks.pop();
            }
        }

        for (auto& v : md->free_conns) {
            SAFE_DELETE(v->c);
            co_release(v->conn_co);
            co_cond_free(v->conn_cond);
        }

        SAFE_DELETE(md);
    }
    m_coroutines.clear();
}

}  // namespace kim