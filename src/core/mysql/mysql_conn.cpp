#include "mysql_conn.h"

#include "error.h"

namespace kim {

MysqlConn::MysqlConn(Log* logger) : Logger(logger) {
}

MysqlConn::~MysqlConn() {
}

bool MysqlConn::connect(db_info_t* db) {
    if (m_conn != nullptr) {
        mysql_close(m_conn);
    }

    char is_reconnect = 1;

    m_conn = mysql_init(m_conn);
    if (m_conn == nullptr) {
        LOG_ERROR("mysql init failed!");
        return false;
    }

    /* https://dev.mysql.com/doc/c-api/8.0/en/mysql-options.html */
    /* https://dev.mysql.com/doc/c-api/8.0/en/c-api-auto-reconnect.html */
    mysql_options(m_conn, MYSQL_OPT_RECONNECT, &is_reconnect);
    mysql_set_character_set(m_conn, db->charset.c_str());
    mysql_options(m_conn, MYSQL_OPT_COMPRESS, NULL);
    mysql_options(m_conn, MYSQL_OPT_LOCAL_INFILE, NULL);

    if (!mysql_real_connect(m_conn, db->host.c_str(), db->user.c_str(),
                            db->password.c_str(), "mysql", db->port, NULL, 0)) {
        LOG_ERROR("db connect failed! %s:%d, error: %d, errstr: %s",
                  db->host.c_str(), db->port, mysql_errno(m_conn), mysql_error(m_conn));
        return false;
    }

    LOG_INFO("mysql connect done! host:%s, port: %d, db name: %s",
             db->host.c_str(), db->port, db->db_name.c_str());
    return true;
}

int MysqlConn::sql_write(const std::string& sql) {
    if (sql.empty()) {
        LOG_ERROR("invalid db params!");
        return ERR_INVALID_PARAMS;
    }

    int ret = sql_exec(sql);
    if (ret != ERR_OK) {
        LOG_ERROR("query sql failed! sql: %s", sql.c_str());
        return ERR_DB_EXEC_FAILED;
    }

    return ERR_OK;
}

int MysqlConn::sql_read(const std::string& sql, vec_row_t& rows) {
    if (sql.empty()) {
        LOG_ERROR("invalid db query params!");
        return ERR_INVALID_PARAMS;
    }

    if (!check_query_sql(sql)) {
        LOG_ERROR("invalid query sql: %s", sql.c_str());
        return ERR_DB_INVALID_QUERY_SQL;
    }

    int ret;
    MYSQL_RES* res;
    MysqlResult result;

    ret = sql_exec(sql);
    if (ret != ERR_OK) {
        LOG_ERROR("query sql failed! sql: %s", sql.c_str());
        return ERR_DB_QUERY_FAILED;
    }

    /* pls malloc/new obj: rows, for safe coroutine's stack! */
    res = mysql_store_result(m_conn);
    if (result.init(m_conn, res)) {
        result.result_data(rows);
    }
    mysql_free_result(res);

    return ERR_OK;
}

int MysqlConn::sql_exec(const std::string& sql) {
    if (sql.empty()) {
        LOG_ERROR("sql is empty.");
        return ERR_INVALID_PARAMS;
    }

    LOG_DEBUG("sql exec, sql: %s.", sql.c_str());

    int ret = mysql_real_query(m_conn, sql.c_str(), sql.length());
    if (ret != 0) {
        ret = mysql_errno(m_conn);
        LOG_ERROR("db query failed! error: %d, errstr: %s",
                  mysql_errno(m_conn), mysql_error(m_conn));
    }

    return ret;
}

bool MysqlConn::check_query_sql(const std::string& sql) {
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

}  // namespace kim