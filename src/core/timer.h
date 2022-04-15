#pragma once

#include "libco/co_routine_inner.h"
#include "server.h"

namespace kim {

#define DEFAULT_TIMER_RATE 10

#define run_with_period(_ms_) if ((_ms_ <= 1000 / m_hz) || !(m_cronloops % ((_ms_) / (1000 / m_hz))))

class TimerCron {
   public:
    TimerCron() = default;
    virtual ~TimerCron() = default;

    virtual void on_repeat_timer() {}
    virtual void on_timer() {
        on_repeat_timer();
        m_cronloops++;
    }

   protected:
    int m_hz = DEFAULT_TIMER_RATE;
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

    void* on_handle_timer(void* arg) {
        co_enable_hook_sys();
        for (;;) {
            co_sleep(1000 / m_hz);
            on_timer();
        }
        return 0;
    }

    bool init_timer() {
        if (m_co_timer == nullptr) {
            co_create(
                &m_co_timer, nullptr,
                [this](void*) { on_handle_timer(this); });
            co_resume(m_co_timer);
        }
        return true;
    }

   protected:
    stCoRoutine_t* m_co_timer = nullptr;
};

}  // namespace kim
