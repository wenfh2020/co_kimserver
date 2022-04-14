#pragma once

#include "../libco/co_routine.h"
#include "../libco/co_routine_inner.h"
#include "../timer.h"
#include "mysql_conn.h"
#include "server.h"

namespace kim {

class MysqlMgr : public Logger, public TimerCron {
    /* sql 任务。*/
    typedef struct task_s {
        stCoRoutine_t* user_co = nullptr; /* 用户协程。*/
        bool is_read = false;             /* 读写操作，是否为读操作。*/
        std::string sql;                  /* sql 命令字符串。*/
        long long active_time = 0;        /* 任务进入处理队列时间。*/
        int ret = 0;                      /* sql 任务处理错误码。*/
        std::string errstr;               /* sql 任务处理错误码字符串。*/
        vec_row_t* rows = nullptr;        /* 读数据库的数据集合。*/
    } task_t;

    struct co_mgr_data_s;

    /* 任务处理器。*/
    typedef struct co_data_s {
        std::shared_ptr<db_info_t> dbi = nullptr;  /* 数据库信息。*/
        stCoCond_t* conn_cond = nullptr;           /* 协程通知唤醒器。*/
        stCoRoutine_t* conn_co = nullptr;          /* 协程结构指针。*/
        std::shared_ptr<MysqlConn> c = nullptr;    /* 数据库链接。*/
        std::queue<std::shared_ptr<task_t>> tasks; /* 待处理 sql 任务。*/
        long long active_time = 0;                 /* 处理器处理当前任务时间，用来捕捉慢日志。*/
    } co_data_t;

    /* 任务分配器。*/
    typedef struct co_mgr_data_s {
        std::shared_ptr<db_info_t> dbi = nullptr;         /* 数据库信息。*/
        stCoRoutine_t* dist_co = nullptr;                 /* 协程结构指针。*/
        stCoCond_t* dist_cond = nullptr;                  /* 协程通知唤醒器。*/
        int conn_cnt = 0;                                 /* 当前已开启连接对应的协程个数。*/
        std::queue<std::shared_ptr<task_t>> tasks;        /* 当前待分配任务个数。*/
        std::list<std::shared_ptr<co_data_t>> busy_conns; /* 正在工作忙碌的协程链接。*/
        std::list<std::shared_ptr<co_data_t>> free_conns; /* 空闲没有处理任务的协程链接。*/
    } co_mgr_data_t;

   public:
    MysqlMgr(std::shared_ptr<Log> logger);
    virtual ~MysqlMgr();

    /*
     * bin/config.json
     * {"database":{"test":{"host":"127.0.0.1","port":3306,"user":"root",
     *                      "password":"xxx","charset":"utf8mb4","max_conn_cnt":3}}}
     */
    bool init(CJsonObject* config);
    void exit() { m_is_exit = true; }

    /**
     * @brief 数据库写数据接口。
     *
     * @param node: define in config.json {"database":{"node":{...}}}
     * @param sql: mysql commnad string.
     *
     * @return error.h / enum E_ERROR.
     */
    int sql_write(const std::string& node, const std::string& sql);

    /**
     * @brief 数据库读数据接口。
     *
     * @param node: define in config.json {"database":{"node":{...}}}
     * @param sql: mysql commnad string.
     * @param rows: query result.
     *
     * @return error.h / enum E_ERROR.
     */
    int sql_read(const std::string& node, const std::string& sql, vec_row_t& rows);

   public:
    /* 定时器调用函数，由上层调用，一般每秒跑 10 次。*/
    virtual void on_repeat_timer() override;

   private:
    void destory();
    /* 任务分配器处理函数。*/
    void on_dist_task(std::shared_ptr<co_mgr_data_t> md);

    /* 任务处理器处理函数。*/
    void on_handle_task(std::shared_ptr<co_mgr_data_t> md, std::shared_ptr<co_data_t> cd);

    /* 发送 sql 任务到数据库连接池处理。*/
    int send_task(const std::string& node, const std::string& sql, bool is_read, vec_row_t* rows = nullptr);

    /* 清空对应任务处理器的待处理任务。*/
    void clear_co_tasks(std::shared_ptr<co_data_t> cd, int ret);

    /* 获取任务处理器。*/
    std::shared_ptr<co_data_t> get_co_data(const std::string& node);
    /* 获取任务分配器。*/
    std::shared_ptr<co_mgr_data_t> get_co_mgr_data(const std::string& node);
    /* 添加空闲的任务处理器到任务分配器。*/
    void add_to_free_list(std::shared_ptr<co_mgr_data_t> md, std::shared_ptr<co_data_t> cd);

   private:
    bool m_is_exit = false;
    int m_old_handle_cnt = 0;
    int m_cur_handle_cnt = 0;
    long long m_slowlog_log_slower_than = 0; /* 慢日志时间，单位为毫秒。*/

    /* key: node, valude: 数据库信息。*/
    std::unordered_map<std::string, std::shared_ptr<db_info_t>> m_dbs;
    /* key: node, value: 任务分配器。*/
    std::unordered_map<std::string, std::shared_ptr<co_mgr_data_t>> m_coroutines;
};

}  // namespace kim
