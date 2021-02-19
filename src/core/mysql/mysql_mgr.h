#ifndef __KIM_MYSQL_MGR_H__
#define __KIM_MYSQL_MGR_H__

#include "../libco/co_routine.h"
#include "../libco/co_routine_inner.h"
#include "mysql_conn.h"
#include "server.h"

namespace kim {

class MysqlMgr : Logger {
    /* cmd task. */
    typedef struct task_s {
        stCoRoutine_t* co = nullptr;
        bool is_read = false;
        std::string sql;
        int ret = 0;
        std::string errstr;
        vec_row_t* query_res_rows = nullptr;
    } task_t;

    /* coroutines arg. */
    typedef struct co_data_s {
        stCoRoutine_t* co = nullptr;
        stCoCond_t* cond = nullptr;
        db_info_t* db = nullptr;
        MysqlConn* c = nullptr;
        void* privdata = nullptr;
        std::queue<task_t*> tasks; /* tasks wait to be handled. */
    } co_data_t;

    typedef struct co_array_data_s {
        db_info_t* db = nullptr;
        std::vector<co_data_t*> coroutines;
    } co_array_data_t;

   public:
    MysqlMgr(Log* logger);
    virtual ~MysqlMgr();

    /**
     * bin/config.json
     * {"database":{"test":{"host":"127.0.0.1","port":3306,"user":"root",
     *                      "password":"xxx","charset":"utf8mb4","max_conn_cnt":3}}}
     */
    bool init(CJsonObject* config);

    /**
     * @brief mysql write interface.
     * 
     * @param node: define in config.json {"database":{"node":{...}}}
     * @param sql: mysql commnad string.
     * 
     * @return error.h / enum E_ERROR.
     */
    int sql_write(const std::string& node, const std::string& sql);

    /**
     * @brief mysql read interface.
     * 
     * @param node: define in config.json {"database":{"node":{...}}}
     * @param sql: mysql commnad string.
     * @param rows: query result.
     * 
     * @return error.h / enum E_ERROR.
     */
    int sql_read(const std::string& node, const std::string& sql, vec_row_t& rows);

   private:
    void destory();

    void clear_co_tasks(co_data_t* cd);
    static void* co_handle_task(void* arg);
    void* handle_task(void* arg);
    co_data_t* get_co_data(const std::string& node, const std::string& obj);
    int send_task(const std::string& node, const std::string& sql, bool is_read, vec_row_t* rows = nullptr);
    void co_sleep(int ms);

   private:
    /* key: node, valude: db info. */
    std::unordered_map<std::string, db_info_t*> m_dbs;
    /* key: node, value: coroutines array data. */
    std::unordered_map<std::string, co_array_data_t*> m_coroutines;
};

}  // namespace kim

#endif  //__KIM_MYSQL_MGR_H__