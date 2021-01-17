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

    co_cond_free(m_sql_task_cond);
    for (const auto& co : m_coroutines) {
        co_release(co);
    }
    m_coroutines.clear();
}

bool MysqlMgr::init(CJsonObject& config) {
    db_info_t* db;
    db_co_task_t* task;
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

    m_sql_task_cond = co_cond_alloc();

    for (auto& it : m_dbs) {
        for (int i = 0; i < it.second->max_conn_cnt; i++) {
            task = new db_co_task_t;
            task->db = it.second;
            task->privdata = this;
            task->c = nullptr;

            co_create(&(task->co), nullptr, co_handler_sql, task);
            co_resume(task->co);
            m_coroutines.insert(task->co);
        }
    }

    return true;
}

void* MysqlMgr::co_handler_sql(void* arg) {
    co_enable_hook_sys();
    db_co_task_t* task = (db_co_task_t*)arg;
    MysqlMgr* d = (MysqlMgr*)task->privdata;
    return d->handler_sql(arg);
}

void* MysqlMgr::handler_sql(void* arg) {
    sql_task_t* sql_task;
    db_co_task_t* co_task;

    co_task = (db_co_task_t*)arg;

    if (co_task->c == nullptr) {
        co_task->c = new MysqlConn(logger());
        while (!co_task->c->connect(co_task->db)) {
            LOG_ERROR("db connect failed! node: %s, host: %s, port: %d",
                      co_task->db->node.c_str(), co_task->db->host.c_str(),
                      co_task->db->port);
            struct pollfd pf = {0};
            pf.fd = -1;
            poll(&pf, 1, 1000);
            continue;
        }
    }

    for (;;) {
        auto it = m_sql_tasks.find(co_task->db->node);
        if (it == m_sql_tasks.end() || it->second.empty()) {
            co_cond_timedwait(m_sql_task_cond, -1);
            continue;
        }

        sql_task = it->second.front();
        it->second.pop();

        if (sql_task->is_read) {
            sql_task->ret = co_task->c->sql_read(sql_task->sql, *sql_task->query_res_rows);
        } else {
            sql_task->ret = co_task->c->sql_write(sql_task->sql);
        }

        co_resume(sql_task->co);
    }

    return 0;
}

int MysqlMgr::send_sql_task(const std::string& node, const std::string& sql, bool is_read, vec_row_t* rows) {
    int ret;
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

    co_cond_signal(m_sql_task_cond);
    co_yield_ct();

    ret = task->ret;
    SAFE_DELETE(task);
    return ret;
}

int MysqlMgr::sql_write(const std::string& node, const std::string& sql) {
    if (node.empty() || sql.empty()) {
        LOG_ERROR("invalid db exec params!");
        return ERR_INVALID_PARAMS;
    }

    return send_sql_task(node, sql, false);
}

int MysqlMgr::sql_read(const std::string& node, const std::string& sql, vec_row_t& rows) {
    if (node.empty() || sql.empty()) {
        LOG_ERROR("invalid db query params!");
        return ERR_INVALID_PARAMS;
    }

    return send_sql_task(node, sql, true, &rows);
}

}  // namespace kim