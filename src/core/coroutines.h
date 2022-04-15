#pragma once

#include "connection.h"
#include "libco/co_routine.h"
#include "server.h"
#include "timer.h"
#include "util/log.h"

const int MAX_CO_CNT = 100000;

namespace kim {

class Coroutines : public Logger, public TimerCron {
   public:
    Coroutines(std::shared_ptr<Log> logger);
    virtual ~Coroutines();

    void destroy();

    void run();
    void exit_libco();
    void set_max_co_cnt(int cnt) { m_max_co_cnt = cnt; }

    bool add_free_co(stCoRoutine_t* co);
    stCoRoutine_t* start_co(pfn_co_routine_t fn);
    virtual void on_repeat_timer() override;

   private:
    void clear();

   private:
    bool m_is_exit = false; /* exit libco. */
    stCoRoutineAttr_t m_co_attr;
    int m_max_co_cnt = MAX_CO_CNT;
    std::set<stCoRoutine_t*> m_work_coroutines;
    std::queue<stCoRoutine_t*> m_free_coroutines;
};

}  // namespace kim
