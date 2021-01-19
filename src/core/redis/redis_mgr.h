
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

    /* coroutines arg info. */
    typedef struct rds_co_data_s {
        stCoRoutine_t* co = nullptr;
        redis_info_t* rds = nullptr;
        redisContext* c = nullptr;
        void* privdata = nullptr;
    } rds_co_data_t;

    /* redis cmd task. */
    typedef struct task_s {
        stCoRoutine_t* co = nullptr;
        std::string cmd;
        redisReply* reply = nullptr;
    } task_t;

   public:
    RedisMgr(Log* logger);
    virtual ~RedisMgr();

   public:
    bool init(CJsonObject& config);
    redisReply* exec_cmd(const std::string& node, const std::string& cmd);

   private:
    void destory();
    redisContext* connect(const std::string& host, int port);

    static void* co_handle_task(void* arg);
    void* handle_task(void* arg);

    redisReply* send_task(const std::string& node, const std::string& cmd);

   private:
    stCoCond_t* m_task_cond = nullptr;
    std::set<rds_co_data_t*> m_co_datas;

    std::unordered_map<std::string, redis_info_t*> m_rds_infos;
    std::unordered_map<std::string, std::queue<task_t*>> m_tasks;
};

}  // namespace kim

#endif  //__KIM_REDIS_MGR_H__