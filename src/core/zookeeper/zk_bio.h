/*
 * create a new thread to handle the zk sync commands in the background,
 * and callback for async.
 */
#pragma once

#include <pthread.h>

#include <list>

#include "server.h"
#include "util/log.h"
#include "zk_task.h"

namespace kim {

class Bio : public Logger {
   public:
    Bio(std::shared_ptr<Log> logger);
    virtual ~Bio();

    /* create a new thread. */
    bool bio_init();
    /* stop thread. */
    void bio_stop() { m_stop_thread = true; }

    /* add async cmd task to handle in bio thread. */
    bool add_cmd_task(const std::string& path, zk_task_t::CMD cmd, const std::string& value = "");
    /* add async ack to handle in main thread (timer.) */
    void add_ack_task(zk_task_t* task);

    /* bio thread. */
    static void* bio_process_tasks(void* arg);
    /* call by bio thread. */
    virtual void bio_process_cmd(zk_task_t* task) {}
    /* call by timer. (async) */
    virtual void timer_process_ack(zk_task_t* task) {}

    /* timer. */
    virtual void on_bio_timer();
    /* async handle task ack. */
    void handle_acks();

   protected:
    pthread_t m_thread;
    pthread_cond_t m_cond;
    pthread_mutex_t m_mutex;

    volatile bool m_stop_thread = true;
    std::list<zk_task_t*> m_req_tasks;
    std::list<zk_task_t*> m_ack_tasks;
};

}  // namespace kim
