#ifndef __KIM_MYSQL_CONN_H__
#define __KIM_MYSQL_CONN_H__

#include "../server.h"
#include "mysql_result.h"

namespace kim {

/* database info. */
typedef struct db_info_s {
    int port = 0;
    int max_conn_cnt = 0;
    std::string host, db_name, password, charset, user, node;
} db_info_t;

class MysqlConn : Logger {
   public:
    MysqlConn(Log* logger);
    virtual ~MysqlConn();

    int sql_write(const std::string& sql);
    int sql_read(const std::string& sql, vec_row_t& rows);

    bool connect(db_info_t* db);
    MYSQL* get_conn() { return m_conn; }
    void close();

   private:
    int sql_exec(const std::string& sql);
    bool check_query_sql(const std::string& sql);

   private:
    MYSQL* m_conn = nullptr;
};

}  // namespace kim

#endif  //__KIM_MYSQL_CONN_H__