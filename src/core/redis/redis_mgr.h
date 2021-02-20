
/* redis pool. */

#ifndef __KIM_REDIS_MGR_H__
#define __KIM_REDIS_MGR_H__

#include <hiredis/hiredis.h>

#include "../coroutines.h"
#include "../libco/co_routine.h"
#include "../libco/co_routine_inner.h"
#include "../server.h"

namespace kim {

class RedisMgr : Logger {
    /* redis info. */
    typedef struct redis_info_s {
        std::string node;
        int max_conn_cnt = 0;
        std::string host;
        int port = 0;
    } redis_info_t;

    /* redis cmd task. */
    typedef struct task_s {
        std::string cmd;             /* redis cmd. */
        stCoRoutine_t* co = nullptr; /* user's coroutine. */
        redisReply* reply = nullptr; /* redis cmd's reply. */
    } task_t;

    /* coroutines arg. */
    typedef struct co_data_s {
        stCoRoutine_t* co = nullptr; /* redis conn's coroutine. */
        stCoCond_t* cond = nullptr;  /* coroutine cond. */
        redis_info_t* ri = nullptr;  /* redis info(host,port...) */
        redisContext* c = nullptr;   /* redis conn. */
        std::queue<task_t*> tasks;   /* tasks wait to be handled. */
        void* privdata = nullptr;    /* user's data. */
    } co_data_t;

    typedef struct co_array_data_s {
        redis_info_t* ri = nullptr; /* redis info(host,port...) */
        int cur_index = 0;
        std::vector<co_data_t*> coroutines;
    } co_array_data_t;

   public:
    RedisMgr(Log* log);
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
     * 
     * @return hiredis result: redisReply*.
     */
    redisReply* exec_cmd(const std::string& node, const std::string& cmd);

   private:
    void destory();
    redisContext* connect(const std::string& host, int port);

    void clear_co_tasks(co_data_t* cd);
    static void* co_handle_task(void* arg);
    void* handle_task(void* arg);
    redisReply* send_task(const std::string& node, const std::string& cmd);
    co_data_t* get_co_data(const std::string& node);
    void co_sleep(int ms);

   private:
    /* key: node, valude: config data. */
    std::unordered_map<std::string, redis_info_t*> m_rds_infos;
    /* key: node, value: conn array data. */
    std::unordered_map<std::string, co_array_data_t*> m_coroutines;
};

}  // namespace kim

#endif  //__KIM_REDIS_MGR_H__