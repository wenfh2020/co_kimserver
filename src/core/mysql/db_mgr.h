#ifndef __KIM_DB_MGR_H__
#define __KIM_DB_MGR_H__

#include "server.h"
#include "mysql_conn.h"

namespace kim {

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
    MYSQL* get_db_conn(const std::string& node);
    bool close_db_conn(MYSQL* c);
    bool check_query_sql(const std::string& sql);
    MYSQL* sql_exec(const std::string& node, const std::string& sql);

   private:
    typedef std::pair<std::list<MysqlConn*>::iterator, std::list<MysqlConn*>> MysqlConnPair;
    std::unordered_map<std::string, MysqlConnPair> m_conns;
    std::unordered_map<std::string, db_info_t*> m_dbs;
};

}  // namespace kim

#endif  //__KIM_DB_MGR_H__