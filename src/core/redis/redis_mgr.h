#pragma once

#include "../error.h"
#include "../libco/co_routine.h"
#include "../libco/co_routine_inner.h"
#include "../server.h"

namespace kim {

class RedisMgr : Logger {
    /* redis info. */
    typedef struct redis_info_s {
        int port = 0;
        std::string host;
        std::string node;
        int max_conn_cnt = 0;
    } redis_info_t;

    /* redis cmd task. */
    typedef struct task_s {
        int ret = ERR_OK;
        std::string cmd;             /* redis cmd. */
        stCoRoutine_t* co = nullptr; /* user's coroutine. */
        redisReply* reply = nullptr; /* redis cmd's reply. */
    } task_t;

    /* coroutines arg. */
    typedef struct co_data_s {
        stCoRoutine_t* co = nullptr;                /* redis conn's coroutine. */
        stCoCond_t* cond = nullptr;                 /* coroutine cond. */
        redisContext* c = nullptr;                  /* redis conn. */
        std::shared_ptr<redis_info_t> ri = nullptr; /* redis info(host,port...) */
        std::queue<std::shared_ptr<task_t>> tasks;  /* tasks wait to be handled. */
        void* privdata = nullptr;                   /* user's data. */
    } co_data_t;

    typedef struct co_array_data_s {
        int cur_idx = 0;
        std::shared_ptr<redis_info_t> ri = nullptr; /* redis info(host,port...) */
        std::vector<std::shared_ptr<co_data_t>> coroutines;
    } co_array_data_t;

   public:
    RedisMgr(std::shared_ptr<Log> log);
    virtual ~RedisMgr();

   public:
    /**
     * ./bin/config json:
     * {"redis":{"test":{"host":"127.0.0.1","port":6379,"max_conn_cnt":1}}}
     */
    bool init(CJsonObject* config);

    /**
     * @brief redis read/write interface.
     *
     * @param node: define in config.json {"redis":{"node":{...}}}
     * @param cmd: redis's commnad string.
     * @param r: redisReply result.
     *
     * @return error.h / enum E_ERROR.
     */
    int exec_cmd(const std::string& node, const std::string& cmd, redisReply** r);

   private:
    void destory();
    std::shared_ptr<co_data_t> get_co_data(const std::string& node);
    redisContext* connect(const std::string& host, int port);

    void on_handle_task(std::shared_ptr<co_data_t> cd);
    int send_task(const std::string& node, const std::string& cmd, redisReply** r);
    void clear_co_tasks(std::shared_ptr<co_data_t> cd);

    void handle_redis_cmd(std::shared_ptr<co_data_t> cd);

   private:
    /* key: node, valude: config data. */
    std::unordered_map<std::string, std::shared_ptr<redis_info_t>> m_rds_infos;
    /* key: node, value: conn array data. */
    std::unordered_map<std::string, std::shared_ptr<co_array_data_t>> m_coroutines;
};

}  // namespace kim
