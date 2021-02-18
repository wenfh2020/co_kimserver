#ifndef __KIM_MYSQL_MGR_H__
#define __KIM_MYSQL_MGR_H__

#include "../libco/co_routine.h"
#include "../libco/co_routine_inner.h"
#include "mysql_conn.h"
#include "server.h"

namespace kim {

class MysqlMgr : Logger {
    /* coroutines arg info. */
    typedef struct db_co_s {
        stCoRoutine_t* co = nullptr;
        db_info_t* db = nullptr;
        MysqlConn* c = nullptr;
        void* privdata = nullptr;
    } db_co_t;

    typedef struct sql_task_s {
        stCoRoutine_t* co = nullptr;
        bool is_read = false;
        std::string sql;
        int err = 0;
        std::string errstr;
        vec_row_t* query_res_rows = nullptr;
    } sql_task_t;

   public:
    MysqlMgr(Log* logger);
    virtual ~MysqlMgr();

    bool init(CJsonObject* config);
    /* write. */
    int sql_write(const std::string& node, const std::string& sql);
    /* read. */
    int sql_read(const std::string& node, const std::string& sql, vec_row_t& rows);

   private:
    void destory();

    static void* co_handle_task(void* arg);
    void* handle_task(void* arg);

    int send_task(const std::string& node, const std::string& sql, bool is_read, vec_row_t* rows = nullptr);

   private:
    stCoCond_t* m_task_cond = nullptr;
    std::set<stCoRoutine_t*> m_coroutines;

    std::unordered_map<std::string, db_info_t*> m_dbs;
    std::unordered_map<std::string, std::queue<sql_task_t*>> m_sql_tasks;
};

}  // namespace kim

#endif  //__KIM_MYSQL_MGR_H__