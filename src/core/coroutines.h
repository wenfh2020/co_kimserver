#pragma once

#include "connection.h"
#include "libco/co_routine.h"
#include "server.h"
#include "timer.h"
#include "util/log.h"

#define MAX_CO_CNT 100000

namespace kim {

typedef struct co_task_s {
    Connection* c;
    stCoRoutine_t* co;
} co_task_t;

class Coroutines : public Logger, public TimerCron {
   public:
    Coroutines(Log* logger);
    virtual ~Coroutines();

    void clear_tasks();
    void run();
    void exit_libco();

    bool add_free_co_task(co_task_t* task);
    co_task_t* create_co_task(Connection* c, pfn_co_routine_t fn);

    int get_max_co_cnt() { return m_max_co_cnt; }
    void set_max_co_cnt(int cnt) { m_max_co_cnt = cnt; }

    int event_loop_handler(void*);
    static int co_event_loop_handler(void*);

    virtual void on_repeat_timer() override;

   private:
    bool m_is_exit = false; /* exit libco. */

    stCoRoutineAttr_t m_co_attr;
    int m_max_co_cnt = MAX_CO_CNT;
    std::set<co_task_t*> m_work_coroutines;
    std::queue<co_task_t*> m_free_coroutines;
};

}  // namespace kim
