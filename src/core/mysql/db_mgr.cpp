#include "db_mgr.h"

#include "error.h"

#define DEF_CONN_CNT 5
#define MAX_CONN_CNT 30

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
            SAFE_DELETE(itr);
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

    int ret;
    MysqlConn* c;

    c = get_db_conn(node);
    if (c == nullptr) {
        LOG_ERROR("get db conn failed, node: %s", node.c_str());
        return ERR_DB_GET_CONNECTION;
    }

    ret = c->sql_write(sql);
    if (ret != ERR_OK) {
        LOG_ERROR("sql write failed! node: %s.", node.c_str());
        return ret;
    }

    return ERR_OK;
}

int DBMgr::sql_read(const std::string& node, const std::string& sql, vec_row_t& rows) {
    if (node.empty() || sql.empty()) {
        LOG_ERROR("invalid db query params!");
        return ERR_INVALID_PARAMS;
    }

    int ret;
    MysqlConn* c;

    c = get_db_conn(node);
    if (c == nullptr) {
        LOG_ERROR("get db conn failed, node: %s", node.c_str());
        return ERR_DB_GET_CONNECTION;
    }

    ret = c->sql_read(sql, rows);
    if (ret != ERR_OK) {
        LOG_ERROR("sql read failed! node: %s.", node.c_str());
        return ret;
    }

    LOG_DEBUG("sql read done! node: %s, sql: %s", node.c_str(), sql.c_str());
    return ERR_OK;
}

MysqlConn* DBMgr::get_db_conn(const std::string& node) {
    auto itr = m_dbs.find(node);
    if (itr == m_dbs.end()) {
        LOG_ERROR("invalid db node: %s!", node.c_str());
        return nullptr;
    }

    MysqlConn* c;
    db_info_t* db;
    std::string conn_id;

    db = itr->second;
    conn_id = format_str("%s:%d:%s", db->host.c_str(), db->port, db->db_name.c_str());

    auto it = m_conns.find(conn_id);
    if (it == m_conns.end()) {
        c = new MysqlConn(logger());
        if (c == nullptr || c->connect(db) == nullptr) {
            LOG_ERROR("db connect failed! host: %s, port: %d", db->host.c_str(), db->port);
            return nullptr;
        }
        std::list<MysqlConn*> conns({c});
        m_conns.insert({conn_id, {conns.begin(), conns}});
        MysqlConnPair& pair = m_conns.begin()->second;
        pair.first = pair.second.begin();
        return c;
    } else {
        auto& list = it->second.second;
        auto& list_itr = it->second.first;
        if ((int)list.size() < db->max_conn_cnt) {
            c = new MysqlConn(logger());
            if (c == nullptr || c->connect(db) == nullptr) {
                LOG_ERROR("db connect failed! host: %s, port: %d", db->host.c_str(), db->port);
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

bool DBMgr::close_db_conn(MysqlConn* c) {
    if (c == nullptr) {
        return false;
    }

    for (auto& it : m_conns) {
        auto& list = it.second.second;
        auto itr = list.begin();
        for (; itr != list.end(); itr++) {
            if (*itr == c) {
                SAFE_DELETE(c);
                list.erase(itr);
                it.second.first = list.begin();
                return true;
            }
        }
    }

    return false;
}

}  // namespace kim