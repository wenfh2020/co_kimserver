#include "mysql_mgr.h"

#include "error.h"

#define DEF_CONN_CNT 5
#define MAX_CONN_CNT 30

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

    cd = get_co_data(node);
    if (cd == nullptr) {
        LOG_ERROR("can not find conn, node: %s", node.c_str());
        return ERR_DB_FAILED;
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

        if (task->is_read) {
            task->ret = cd->c->sql_read(task->sql, *task->query_res_rows);
        } else {
            task->ret = cd->c->sql_write(task->sql);
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
            cd = ad->coroutines[ad->cur_index % ad->coroutines.size()];
            if (++ad->cur_index == (int)ad->coroutines.size()) {
                ad->cur_index = 0;
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

bool MysqlMgr::init(CJsonObject* config) {
    if (config == nullptr) {
        LOG_ERROR("invalid params!");
        return false;
    }

    db_info_t* db;
    std::vector<std::string> vec;

    config->GetKeys(vec);
    if (vec.size() == 0) {
        LOG_ERROR("database info is empty.");
        return false;
    }

    for (const auto& v : vec) {
        const CJsonObject& obj = (*config)[v];

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
            SAFE_DELETE(v->db);
            while (!v->tasks.empty()) {
                SAFE_DELETE(v->tasks.front());
                v->tasks.pop();
            }
        }
    }
    m_coroutines.clear();
}

void MysqlMgr::co_sleep(int ms) {
    struct pollfd pf = {0};
    pf.fd = -1;
    poll(&pf, 1, ms);
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