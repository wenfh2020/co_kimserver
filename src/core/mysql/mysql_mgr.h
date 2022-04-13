#pragma once

#include "../libco/co_routine.h"
#include "../libco/co_routine_inner.h"
#include "../timer.h"
#include "mysql_conn.h"
#include "server.h"

namespace kim {

class MysqlMgr : Logger, public TimerCron {
    /* sql task. */
    typedef struct task_s {
        stCoRoutine_t* user_co = nullptr; /* user's coroutine. */
        bool is_read = false;             /* read or write sql. */
        std::string sql;                  /* sql command. */
        long long active_time = 0;        /* time to load into the queue. */
        int ret = 0;                      /* result code. */
        std::string errstr;               /* result string. */
        vec_row_t* rows = nullptr;        /* result - query rows. */
    } task_t;

    struct co_mgr_data_s;

    /* connection's data. */
    typedef struct co_data_s {
        db_info_t* db = nullptr;            /* database info. */
        struct co_mgr_data_s* md = nullptr; /* manager of connection's data. */
        stCoCond_t* conn_cond = nullptr;    /* notification for handling tasks. */
        stCoRoutine_t* conn_co = nullptr;   /* coroutine for handling tasks. */
        MysqlConn* c = nullptr;             /* db connection. */
        std::queue<task_t*> tasks;          /* tasks queue. */
        long long active_time = 0;          /* last working time. */
        void* privdata = nullptr;           /* private data. */
    } co_data_t;

    /* manager of connections. */
    typedef struct co_mgr_data_s {
        db_info_t* db = nullptr;          /* database info. */
        stCoRoutine_t* dist_co = nullptr; /* coroutine for distribute tasks. */
        stCoCond_t* dist_cond = nullptr;  /* notify for distribute tasks. */
        void* privdata = nullptr;         /* private data. */
        int conn_cnt = 0;                 /* connection count. */
        std::list<co_data_t*> busy_conns; /* working connections. */
        std::list<co_data_t*> free_conns; /* free connections. */
        std::queue<task_t*> tasks;        /* tasks waiting to distribute. */
    } co_mgr_data_t;

   public:
    MysqlMgr(std::shared_ptr<Log> logger);
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

   public:
    /* call by parent, 10 times/second on Linux. */
    virtual void on_repeat_timer() override;

   private:
    void destory();

    /* coroutine for distribution of tasks. */
    void* on_dist_task(void* arg);

    /* coroutine for handling task. */
    void* on_handle_task(void* arg);

    /* put task into task's queue. */
    int send_task(const std::string& node, const std::string& sql, bool is_read, vec_row_t* rows = nullptr);

    /* clear conn's waiting tasks.  */
    void clear_co_tasks(co_data_t* cd, int ret);

    /* get db conn's data. */
    co_data_t* get_co_data(const std::string& node);
    /* get conn's manager data. */
    co_mgr_data_t* get_co_mgr_data(const std::string& node);
    /* add conn data to free conn's list, when no task. */
    void add_to_free_list(co_data_t* cd);

   private:
    int m_old_handle_cnt = 0;
    int m_cur_handle_cnt = 0;
    long long m_slowlog_log_slower_than = 0; /* slow log time. */

    /* key: node, valude: database info. */
    std::unordered_map<std::string, db_info_t*> m_dbs;
    /* key: node, value: coroutine's manager data. */
    std::unordered_map<std::string, co_mgr_data_t*> m_coroutines;
};

}  // namespace kim
