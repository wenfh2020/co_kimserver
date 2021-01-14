#ifndef __KIM_DB_MGR_H__
#define __KIM_DB_MGR_H__

#include "mysql_result.h"
#include "server.h"

namespace kim {

/* database info. */
typedef struct db_info_s {
    int port = 0;
    int max_conn_cnt = 0;
    std::string host, db_name, password, charset, user;
} db_info_t;

class DBMgr : Logger {
   public:
    DBMgr(Log* logger);
    virtual ~DBMgr();

    bool init(CJsonObject& config);
    /* write. */
    int sql_write(const std::string& node, const std::string& sql);
    /* read. */
    int sql_read(const std::string& node, const std::string& sql, vec_row_t& rows);

   private:
    void destory_db_infos();
    MYSQL* db_connect(db_info_t* db);
    MYSQL* get_db_conn(const std::string& node);
    bool check_query_sql(const std::string& sql);
    bool sql_exec(MYSQL* c, const std::string& node, const std::string& sql);

   private:
    typedef std::pair<std::list<MYSQL*>::iterator, std::list<MYSQL*>> MysqlConnPair;
    std::unordered_map<std::string, MysqlConnPair> m_conns;
    std::unordered_map<std::string, db_info_t*> m_dbs;
};

}  // namespace kim

#endif  //__KIM_DB_MGR_H__