#include "mysql_conn.h"

namespace kim {

MysqlConn::MysqlConn(Log* logger) : Logger(logger) {

}
 
MysqlConn::~MysqlConn() {

}

MYSQL* MysqlConn::connect(db_info_t* db) {
    if (m_conn != nullptr) {
        mysql_close(m_conn);
    }

    char is_reconnect = 1;
    unsigned int timeout = MYSQL_CONN_TIMEOUT_SECS;

    m_conn = mysql_init(m_conn);
    if (m_conn == nullptr) {
        LOG_ERROR("mysql init failed!");
        return nullptr;
    }

    mysql_options(m_conn, MYSQL_OPT_COMPRESS, NULL);
    mysql_options(m_conn, MYSQL_OPT_LOCAL_INFILE, NULL);
    if (!mysql_real_connect(m_conn, db->host.c_str(), db->user.c_str(),
                            db->password.c_str(), "mysql", db->port, NULL, 0)) {
        LOG_ERROR("db connect failed! %s:%d, error: %d, errstr: %s",
                  db->host.c_str(), db->port, mysql_errno(m_conn), mysql_error(m_conn));
        return nullptr;
    }

    /* https://dev.mysql.com/doc/c-api/8.0/en/mysql-options.html */
    /* https://dev.mysql.com/doc/c-api/8.0/en/c-api-auto-reconnect.html */
    mysql_options(m_conn, MYSQL_OPT_CONNECT_TIMEOUT, reinterpret_cast<char*>(&timeout));
    mysql_options(m_conn, MYSQL_OPT_RECONNECT, &is_reconnect);
    mysql_set_character_set(m_conn, db->charset.c_str());

    LOG_INFO("mysql connect done! host:%s, port: %d, db name: %s",
             db->host.c_str(), db->port, db->db_name.c_str());
    return m_conn;
}


int MysqlConn::sql_write(const std::string& sql);

int MysqlConn::sql_read(const std::string& sql, vec_row_t& rows);


}