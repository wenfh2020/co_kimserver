#ifndef __KIM_DB_MGR_H__
#define __KIM_DB_MGR_H__

#include "../libco/co_routine.h"
#include "../libco/co_routine_inner.h"
#include "mysql_conn.h"
#include "server.h"

namespace kim {

typedef struct sql_task_s {
    stCoRoutine_t* co;
    bool is_read;
    std::string sql;
    int ret;
    std::string errstr;
    vec_row_t* query_res_rows;
} sql_task_t;

typedef struct db_co_task_s {
    stCoRoutine_t* co;
    MysqlConn* c;
    db_info_t* db;
    void* privdata;
} db_co_task_t;

typedef struct co_resume_data_s {
    stCoRoutine_t* co;
    void* privdata;
} co_resume_data_t;

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

    static void* co_handler_sql(void* arg);
    void* handler_sql(void* arg);

    static void* co_handler_resume(void* arg);
    void* handler_resume(void* arg);

    int send_sql_task(const std::string& node, const std::string& sql, bool is_read, vec_row_t* rows = nullptr);

   private:
    std::unordered_map<std::string, db_info_t*> m_dbs;
    stCoCond_t* m_sql_task_cond = nullptr;
    stCoCond_t* m_sql_task_wait_resume_cond = nullptr;
    std::unordered_map<std::string, std::queue<sql_task_t*>> m_sql_tasks;
    std::queue<sql_task_t*> m_wait_resume_tasks;
};

}  // namespace kim

#endif  //__KIM_DB_MGR_H__