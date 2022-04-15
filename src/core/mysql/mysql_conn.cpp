#include "mysql_conn.h"

#include "error.h"

namespace kim {

MysqlConn::MysqlConn(std::shared_ptr<Log> logger) : Logger(logger) {
}

MysqlConn::~MysqlConn() {
    close();
}

void MysqlConn::close() {
    if (m_conn != nullptr) {
        mysql_close(m_conn);
        m_conn = nullptr;
    }
}

bool MysqlConn::connect(std::shared_ptr<db_info_t> dbi) {
    if (dbi == nullptr) {
        LOG_ERROR("invalid db info");
        return false;
    }

    if (m_conn != nullptr) {
        mysql_close(m_conn);
        m_conn = nullptr;
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
    mysql_set_character_set(m_conn, dbi->charset.c_str());
    /* MYSQL_OPT_COMPRESS --> Cost performance. */
    // mysql_options(m_conn, MYSQL_OPT_COMPRESS, NULL);
    // mysql_options(m_conn, MYSQL_OPT_LOCAL_INFILE, NULL);

    if (!mysql_real_connect(m_conn, dbi->host.c_str(), dbi->user.c_str(),
                            dbi->password.c_str(), "mysql", dbi->port, NULL, 0)) {
        LOG_ERROR("db connect failed! %s:%d, error: %d, errstr: %s",
                  dbi->host.c_str(), dbi->port, mysql_errno(m_conn), mysql_error(m_conn));
        return false;
    }

    LOG_INFO("mysql connect done! host:%s, port: %d, dbi name: %s",
             dbi->host.c_str(), dbi->port, dbi->db_name.c_str());
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

/* rows 如果是临时变量，有可能会栈溢出。 */
int MysqlConn::sql_read(const std::string& sql, std::shared_ptr<VecMapRow> rows) {
    if (sql.empty()) {
        LOG_ERROR("invalid db query params!");
        return ERR_INVALID_PARAMS;
    }

    if (!check_query_sql(sql)) {
        LOG_ERROR("invalid query sql: %s", sql.c_str());
        return ERR_DB_INVALID_QUERY_SQL;
    }

    int ret = sql_exec(sql);
    if (ret != ERR_OK) {
        LOG_ERROR("query sql failed! sql: %s", sql.c_str());
        return ERR_DB_QUERY_FAILED;
    }

    MysqlResult result;
    MYSQL_RES* res = mysql_store_result(m_conn);
    if (result.init(m_conn, res)) {
        result.fetch_result_rows(rows);
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