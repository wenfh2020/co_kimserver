
#ifndef __KIM_REDIS_MGR_H__
#define __KIM_REDIS_MGR_H__

#include <hiredis/hiredis.h>

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
    typedef struct rds_co_data_s {
        stCoCond_t* cond = nullptr;  /* coroutine cond. */
        stCoRoutine_t* co = nullptr; /* redis conn's coroutine. */
        redis_info_t* rds = nullptr; /* redis info(host,port...) */
        redisContext* c = nullptr;   /* redis conn. */
        std::queue<task_t*> tasks;   /* tasks wait to be handled. */
        void* privdata = nullptr;
    } rds_co_data_t;

   public:
    RedisMgr(Log* logger);
    virtual ~RedisMgr();

   public:
    bool init(CJsonObject& config);
    redisReply* exec_cmd(const std::string& node, const std::string& cmd);

   private:
    void destory();
    void co_sleep(int ms);
    void wait_connect(rds_co_data_t* rds_co);
    bool del_valid_connect(rds_co_data_t* rds_co);
    bool add_valid_connect(rds_co_data_t* rds_co);
    redisContext* connect(const std::string& host, int port);

    static void* co_handle_task(void* arg);
    void* handle_task(void* arg);
    redisReply* send_task(const std::string& node, const std::string& cmd);

   private:
    std::unordered_map<std::string, redis_info_t*> m_rds_infos;
    std::unordered_map<std::string, std::vector<rds_co_data_t*>> m_valid_coroutines;
    std::list<rds_co_data_t*> m_all_coroutines;
};

}  // namespace kim

#endif  //__KIM_REDIS_MGR_H__