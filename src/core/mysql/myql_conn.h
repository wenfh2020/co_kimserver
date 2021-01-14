#ifndef __KIM_MYSQL_CONN_H__
#define __KIM_MYSQL_CONN_H__

#include "../server.h"
#include "mysql_result.h"

namespace kim {

/* database info. */
typedef struct db_info_s {
    int port = 0;
    int max_conn_cnt = 0;
    std::string host, db_name, password, charset, user;
} db_info_t;

class MysqlConn : Logger {
public:
    MysqlConn(Log* logger);
    virtual ~MysqlConn();

    /* write. */
    int sql_write(const std::string& sql);
    /* read. */
    int sql_read(const std::string& sql, vec_row_t& rows);

    MYSQL* connect(db_info_t* db);
    MYSQL* get_conn() { return m_conn; }

private:
    MYSQL* m_conn = nullptr;
};

}

#endif //__KIM_MYSQL_CONN_H__