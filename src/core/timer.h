#ifndef __KIM_TIMER_H__
#define __KIM_TIMER_H__

#include "libco/co_routine_inner.h"
#include "server.h"

namespace kim {

#define run_with_period(_ms_) if ((_ms_ <= 1000 / m_hz) || !(m_cronloops % ((_ms_) / (1000 / m_hz))))

class TimerCron {
   public:
    TimerCron() {}
    virtual ~TimerCron() {}

   protected:
    int m_hz = 10;
    int m_cronloops = 1;
};

class CoTimer : public TimerCron {
   public:
    CoTimer() {}
    virtual ~CoTimer() {
        if (m_co_timer != nullptr) {
            co_release(m_co_timer);
            m_co_timer = nullptr;
        }
    }

    /* timer's frequency. */
    void set_hz(int hz) { m_hz = hz; }
    virtual void on_repeat_timer() {}

    bool init_timer() {
        if (m_co_timer == nullptr) {
            co_create(&m_co_timer, nullptr, co_handle_timer, this);
            co_resume(m_co_timer);
        }
        return true;
    }

    static void* co_handle_timer(void* arg) {
        co_enable_hook_sys();
        CoTimer* m = (CoTimer*)arg;
        for (;;) {
            co_sleep(1000 / m->m_hz);
            m->on_repeat_timer();
        }
        return 0;
    }

   protected:
    stCoRoutine_t* m_co_timer = nullptr;
};

}  // namespace kim

#endif  //__KIM_TIMER_H__