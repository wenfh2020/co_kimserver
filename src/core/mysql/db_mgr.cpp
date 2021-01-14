#include "db_mgr.h"

#include "error.h"

#define DEF_CONN_CNT 5
#define MAX_CONN_CNT 30
#define MYSQL_CONN_TIMEOUT_SECS 30000

namespace kim {

DBMgr::DBMgr(Log* logger) : Logger(logger) {
}

DBMgr::~DBMgr() {
    destory_db_infos();
}

void DBMgr::destory_db_infos() {
    for (auto& it : m_conns) {
        auto& list = it.second.second;
        for (auto& itr : list) {
            mysql_close(itr);
        }
    }
    m_conns.clear();

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

    return true;
}

int DBMgr::sql_write(const std::string& node, const std::string& sql) {
    if (node.empty() || sql.empty()) {
        LOG_ERROR("invalid db exec params!");
        return ERR_INVALID_PARAMS;
    }

    MYSQL* c = sql_exec(node, sql);
    if (c == nullptr) {
        LOG_ERROR("query sql failed! node: %s, sql: %s", node.c_str(), sql.c_str());
        return ERR_DB_EXEC_FAILED;
    }

    return ERR_OK;
}

int DBMgr::sql_read(const std::string& node, const std::string& sql, vec_row_t& rows) {
    if (node.empty() || sql.empty()) {
        LOG_ERROR("invalid db query params!");
        return ERR_INVALID_PARAMS;
    }

    if (!check_query_sql(sql)) {
        LOG_ERROR("invalid query sql: %s", sql.c_str());
        return ERR_DB_INVALID_QUERY_SQL;
    }

    MYSQL* c;
    MYSQL_RES* res;
    MysqlResult result;

    c = sql_exec(node, sql);
    if (c == nullptr) {
        LOG_ERROR("query sql failed! node: %s, sql: %s", node.c_str(), sql.c_str());
        return ERR_DB_QUERY_FAILED;
    }

    res = mysql_store_result(c);
    if (result.init(c, res)) {
        result.result_data(rows);
    }
    mysql_free_result(res);

    LOG_DEBUG("sql read done! node: %s, sql: %s", node.c_str(), sql.c_str());
    return ERR_OK;
}

bool DBMgr::check_query_sql(const std::string& sql) {
    /* split the first world. */
    std::stringstream ss(sql);
    std::string oper;
    ss >> oper;

    /* find the select's word. */
    const char* select[] = {"select", "show", "explain", "desc"};
    for (int i = 0; i < 4; i++) {
        if (!strcasecmp(oper.c_str(), select[i])) {
            return true;
        }
    }
    return false;
}

MYSQL* DBMgr::get_db_conn(const std::string& node) {
    auto itr = m_dbs.find(node);
    if (itr == m_dbs.end()) {
        LOG_ERROR("invalid db node: %s!", node.c_str());
        return nullptr;
    }

    MYSQL* c;
    db_info_t* db;
    std::string conn_id;

    db = itr->second;
    conn_id = format_str("%s:%d:%s", db->host.c_str(), db->port, db->db_name.c_str());

    auto it = m_conns.find(conn_id);
    if (it == m_conns.end()) {
        c = db_connect(db);
        if (c == nullptr) {
            LOG_ERROR("db connect failed! %s:%d, error: %d, errstr: %s",
                      db->host.c_str(), db->port, mysql_errno(c), mysql_error(c));
            return nullptr;
        }
        std::list<MYSQL*> conns({c});
        m_conns.insert({conn_id, {conns.begin(), conns}});
        MysqlConnPair& pair = m_conns.begin()->second;
        pair.first = pair.second.begin();
        return c;
    } else {
        auto& list = it->second.second;
        auto& list_itr = it->second.first;
        if ((int)list.size() < db->max_conn_cnt) {
            c = db_connect(db);
            if (c == nullptr) {
                LOG_ERROR("db connect failed! %s:%d, error: %d, errstr: %s",
                          db->host.c_str(), db->port, mysql_errno(c), mysql_error(c));
                return nullptr;
            }
            list.push_back(c);
        } else {
            if (++list_itr == list.end()) {
                list_itr = list.begin();
            }
            c = *list_itr;
        }
    }

    return c;
}

bool DBMgr::close_db_conn(MYSQL* c) {
    if (c == nullptr) {
        return false;
    }

    mysql_close(c);

    for (auto& it : m_conns) {
        auto& list = it.second.second;
        auto itr = list.begin();
        for (; itr != list.end(); itr++) {
            if (*itr == c) {
                list.erase(itr);
                it.second.first = list.begin();
                return true;
            }
        }
    }

    return false;
}

MYSQL* DBMgr::sql_exec(const std::string& node, const std::string& sql) {
    if (node.empty() || sql.empty()) {
        return nullptr;
    }

    LOG_DEBUG("sql exec, node: %s, sql: %s", node.c_str(), sql.c_str());

    MYSQL* c;
    int ret, error;

    c = get_db_conn(node);
    if (c == nullptr) {
        LOG_ERROR("get db conn failed! node: %s", node.c_str());
        return nullptr;
    }
    
    ret = mysql_real_query(c, sql.c_str(), sql.length());
    if (ret != 0) {
        error = mysql_errno(c);
        if (error != CR_SERVER_LOST && error != CR_SERVER_GONE_ERROR) {
            LOG_ERROR("db query failed! node: %s, error: %d, errstr: %s",
                      node.c_str(), mysql_errno(c), mysql_error(c));
            return nullptr;
        }

        if (!close_db_conn(c)) {
            LOG_ERROR("close db connection failed! node: %s", node.c_str());
            return nullptr;
        }

        /* reconnect. */
        // ret = mysql_ping(c);
        c = get_db_conn(node);
        if (c == nullptr) {
            LOG_ERROR("db reconnect failed! node: %s, error: %d, errstr: %s",
                      node.c_str(), mysql_errno(c), mysql_error(c));
            return nullptr;
        }

        ret = mysql_real_query(c, sql.c_str(), sql.length());
        if (ret != 0) {
            LOG_ERROR("db query failed! node: %s, error: %d, errstr: %s",
                      node.c_str(), mysql_errno(c), mysql_error(c));
            return nullptr;
        }
    }

    return c;
}

}  // namespace kim