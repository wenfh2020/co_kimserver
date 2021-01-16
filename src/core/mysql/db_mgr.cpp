#include "db_mgr.h"

#include "error.h"

#define DEF_CONN_CNT 5
#define MAX_CONN_CNT 30

namespace kim {

DBMgr::DBMgr(Log* logger) : Logger(logger) {
}

DBMgr::~DBMgr() {
    destory_db_infos();
    co_cond_free(m_sql_task_cond);
    co_cond_free(m_sql_task_wait_resume_cond);
    for (auto& it : m_sql_tasks) {
        std::queue<sql_task_t*>& que = it.second;
        while (!que.empty()) {
            SAFE_DELETE(que.front());
            que.pop();
        }
    }
    m_sql_tasks.clear();
}

void DBMgr::destory_db_infos() {
    for (auto& it : m_dbs) {
        SAFE_DELETE(it.second);
    }
    m_dbs.clear();
}

bool DBMgr::init(CJsonObject& config) {
    std::vector<std::string> vec;
    config.GetKeys(vec);

    /*
        bin/config.json
        {"database":{"test":{"host":"127.0.0.1","port":3306,"user":"root","password":"123456","charset":"utf8mb4","max_conn_cnt":3}}}
    */
    for (const auto& it : vec) {
        const CJsonObject& obj = config[it];
        db_info_t* db = new db_info_t;

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
            destory_db_infos();
            return false;
        }

        m_dbs.insert({it, db});
    }

    stCoRoutineAttr_t attr;
    attr.share_stack = co_alloc_sharestack(16, 16 * 1024 * 1024);
    attr.stack_size = 0;

    m_sql_task_cond = co_cond_alloc();
    m_sql_task_wait_resume_cond = co_cond_alloc();

    for (auto& it : m_dbs) {
        for (int i = 0; i < it.second->max_conn_cnt; i++) {
            db_co_task_t* task = new db_co_task_t;
            task->db = it.second;
            task->privdata = this;
            task->c = nullptr;
            co_create(&(task->co), &attr, co_handler_sql, task);
            co_resume(task->co);

            co_resume_data_t* data = new co_resume_data_t;
            data->privdata = this;
            co_create(&data->co, &attr, co_handler_resume, data);
            co_resume(data->co);
            SAFE_DELETE(data);
        }
    }

    return true;
}

void* DBMgr::co_handler_sql(void* arg) {
    co_enable_hook_sys();
    db_co_task_t* task = (db_co_task_t*)arg;
    DBMgr* d = (DBMgr*)task->privdata;
    return d->handler_sql(arg);
}

void* DBMgr::handler_sql(void* arg) {
    int hanle_cnt = 0;
    db_co_task_t* co_task;
    sql_task_t* sql_task;

    co_task = (db_co_task_t*)arg;

    if (co_task->c == nullptr) {
        co_task->c = new MysqlConn(logger());
        if (co_task->c->connect(co_task->db) == nullptr) {
            SAFE_DELETE(co_task->c);
            LOG_ERROR("db connect failed! host: %s, port: %d",
                      co_task->db->host.c_str(), co_task->db->port);
            return 0;
        }
    }

    for (;;) {
        auto it = m_sql_tasks.find(co_task->db->node);
        if ((it == m_sql_tasks.end() || it->second.empty()) || hanle_cnt >= 300) {
            if (hanle_cnt > 0) {
                hanle_cnt = 0;
                co_cond_signal(m_sql_task_wait_resume_cond);
                if (it != m_sql_tasks.end() && !it->second.empty()) {
                    continue;
                }
            }
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

        hanle_cnt++;
        m_wait_resume_tasks.push(sql_task);
    }

    return 0;
}

void* DBMgr::co_handler_resume(void* arg) {
    co_enable_hook_sys();
    co_resume_data_t* data = (co_resume_data_t*)arg;
    DBMgr* d = (DBMgr*)data->privdata;
    return d->handler_resume(arg);
}

void* DBMgr::handler_resume(void* arg) {
    sql_task_t* sql_task;

    for (;;) {
        if (m_wait_resume_tasks.empty()) {
            co_cond_timedwait(m_sql_task_wait_resume_cond, -1);
            continue;
        }
        sql_task = m_wait_resume_tasks.front();
        m_wait_resume_tasks.pop();
        co_resume(sql_task->co);
    }

    return 0;
}

int DBMgr::send_sql_task(const std::string& node, const std::string& sql, bool is_read, vec_row_t* rows) {
    int ret;
    sql_task_t* task = new sql_task_t;

    task->co = GetCurrThreadCo();
    task->is_read = is_read;
    task->sql = sql;
    task->query_res_rows = rows;

    auto it = m_sql_tasks.find(node);
    if (it == m_sql_tasks.end()) {
        std::queue<sql_task_t*> q;
        q.push(task);
        m_sql_tasks[node] = q;
    } else {
        it->second.push(task);
    }

    co_cond_signal(m_sql_task_cond);
    co_yield_ct();

    ret = task->ret;
    SAFE_DELETE(task);
    return ret;
}

int DBMgr::sql_write(const std::string& node, const std::string& sql) {
    if (node.empty() || sql.empty()) {
        LOG_ERROR("invalid db exec params!");
        return ERR_INVALID_PARAMS;
    }

    return send_sql_task(node, sql, false);
}

int DBMgr::sql_read(const std::string& node, const std::string& sql, vec_row_t& rows) {
    if (node.empty() || sql.empty()) {
        LOG_ERROR("invalid db query params!");
        return ERR_INVALID_PARAMS;
    }

    return send_sql_task(node, sql, true, &rows);
}

}  // namespace kim