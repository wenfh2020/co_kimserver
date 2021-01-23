#ifndef __KIM_TIMER_H__
#define __KIM_TIMER_H__

#include "libco/co_routine_inner.h"
#include "server.h"

namespace kim {

class CoTimer {
   protected:
    stCoRoutine_t* m_co_timer = nullptr;

   public:
    CoTimer() {}
    virtual ~CoTimer() {
        if (m_co_timer != nullptr) {
            co_release(m_co_timer);
        }
    }

    bool init_timer() {
        if (m_co_timer == nullptr) {
            co_create(&m_co_timer, nullptr, co_handle_timer, this);
            co_resume(m_co_timer);
        }
        return true;
    }

    virtual void on_repeat_timer() {}

    static void* co_handle_timer(void* arg) {
        co_enable_hook_sys();
        CoTimer* m = (CoTimer*)arg;
        for (;;) {
            struct pollfd pf = {0};
            pf.fd = -1;
            poll(&pf, 1, 100);
            m->on_repeat_timer();
        }
    }
};

}  // namespace kim

#endif  //__KIM_TIMER_H__