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
    auto it_node = m_dbs.find(node);
    if (it_node == m_dbs.end()) {
        LOG_ERROR("can not find node: %s.", node.c_str());
        return ERR_DB_CAN_NOT_FIND_NODE;
    }

    int err;
    sql_task_t* task;

    task = new sql_task_t;
    task->co = GetCurrThreadCo();
    task->is_read = is_read;
    task->sql = sql;
    task->query_res_rows = rows;

    auto it = m_sql_tasks.find(node);
    if (it == m_sql_tasks.end()) {
        std::queue<sql_task_t*> que;
        que.push(task);
        m_sql_tasks[node] = que;
    } else {
        it->second.push(task);
    }

    co_cond_signal(m_task_cond);
    co_yield_ct();

    err = task->err;
    SAFE_DELETE(task);
    return err;
}

void* MysqlMgr::co_handle_task(void* arg) {
    co_enable_hook_sys();
    db_co_t* db_co = (db_co_t*)arg;
    MysqlMgr* d = (MysqlMgr*)db_co->privdata;
    return d->handle_task(arg);
}

void* MysqlMgr::handle_task(void* arg) {
    db_co_t* db_co;
    sql_task_t* sql_task;

    db_co = (db_co_t*)arg;

    if (db_co->c == nullptr) {
        db_co->c = new MysqlConn(logger());
        while (!db_co->c->connect(db_co->db)) {
            LOG_ERROR("db connect failed! node: %s, host: %s, port: %d",
                      db_co->db->node.c_str(), db_co->db->host.c_str(),
                      db_co->db->port);
            struct pollfd pf = {0};
            pf.fd = -1;
            poll(&pf, 1, 1000);
            continue;
        }
    }

    for (;;) {
        auto it = m_sql_tasks.find(db_co->db->node);
        if (it == m_sql_tasks.end() || it->second.empty()) {
            co_cond_timedwait(m_task_cond, -1);
            continue;
        }

        sql_task = it->second.front();
        it->second.pop();

        if (sql_task->is_read) {
            sql_task->err = db_co->c->sql_read(sql_task->sql, *sql_task->query_res_rows);
        } else {
            sql_task->err = db_co->c->sql_write(sql_task->sql);
        }

        co_resume(sql_task->co);
    }

    return 0;
}

bool MysqlMgr::init(CJsonObject& config) {
    db_info_t* db;
    db_co_t* db_co;
    std::vector<std::string> vec;

    config.GetKeys(vec);

    /*
        bin/config.json
        {"database":{"test":{"host":"127.0.0.1","port":3306,"user":"root","password":"123456","charset":"utf8mb4","max_conn_cnt":3}}}
    */
    if (vec.size() == 0) {
        LOG_ERROR("database info is empty.");
        return false;
    }

    for (const auto& it : vec) {
        const CJsonObject& obj = config[it];
        db = new db_info_t;

        db->host = obj("host");
        db->db_name = obj("name").empty() ? "mysql" : obj("name");
        db->password = obj("password");
        db->charset = obj("charset");
        db->user = obj("user");
        db->port = str_to_int(obj("port"));
        db->max_conn_cnt = str_to_int(obj("max_conn_cnt"));
        db->node = it;

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
            LOG_ERROR("invalid db node info: %s", it.c_str());
            SAFE_DELETE(db);
            destory();
            return false;
        }

        m_dbs.insert({it, db});
    }

    m_task_cond = co_cond_alloc();

    for (auto& it : m_dbs) {
        for (int i = 0; i < it.second->max_conn_cnt; i++) {
            db_co = new db_co_t;
            db_co->db = it.second;
            db_co->privdata = this;
            db_co->c = nullptr;

            co_create(&(db_co->co), nullptr, co_handle_task, db_co);
            co_resume(db_co->co);
            m_coroutines.insert(db_co->co);
        }
    }

    return true;
}

void MysqlMgr::destory() {
    for (auto& it : m_dbs) {
        SAFE_DELETE(it.second);
    }
    m_dbs.clear();

    for (auto& it : m_sql_tasks) {
        std::queue<sql_task_t*>& que = it.second;
        while (!que.empty()) {
            SAFE_DELETE(que.front());
            que.pop();
        }
    }
    m_sql_tasks.clear();

    co_cond_free(m_task_cond);
    for (const auto& co : m_coroutines) {
        co_release(co);
    }
    m_coroutines.clear();
}

}  // namespace kim