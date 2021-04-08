#ifndef __KIM_TIMERS_H__
#define __KIM_TIMERS_H__

#include "server.h"
#include "timer.h"

namespace kim {

/* timer's group id (first: due time, second: timer id)*/
typedef std::pair<int64_t, uint32_t> TimerGrpID;

/* timer's callback function. 
   first arg: timer's id, second arg: is repeat, third: privdata. */
typedef std::function<void(int, bool, void*)> TimerEvent;

// Timer
////////////////////////////////////////////////

class Timer {
   public:
    Timer() {}
    Timer(int id, const TimerEvent& fn, uint64_t after, uint64_t repeat, void* privdata);
    virtual ~Timer() {}

    int id() { return m_id; }
    void set_id(int id) { m_id = id; }

    void* privdata() { return m_privdata; }
    void set_privdata(void* data) { m_privdata = data; }

    uint64_t after_time() { return m_after_time; }
    void set_after_time(uint64_t after) { m_after_time = after; }

    uint64_t repeat_time() { return m_repeat_time; }
    void set_repeat_time(uint64_t repeat) { m_repeat_time = repeat; }

    TimerEvent& callback_fn() { return m_callback_fn; }
    void set_callback_fn(const TimerEvent& fn) { m_callback_fn = fn; }

   protected:
    int m_id = 0;               /* timer's id. */
    uint64_t m_after_time = 0;  /* timeout in `after` milliseconds. */
    uint64_t m_repeat_time = 0; /* repeat milliseconds. */
    void* m_privdata = nullptr; /* user's data. */
    TimerEvent m_callback_fn;   /* callback function. */
};

// Timers
////////////////////////////////////////////////

class Timers : public Logger, public CoTimer {
   public:
    Timers(Log* logger) : Logger(logger) {}
    virtual ~Timers();

    Timers(const Timers&) = delete;
    Timers& operator=(const Timers&) = delete;

   public:
    bool del_timer(int id);
    int add_timer(const TimerEvent& fn, uint64_t after, uint64_t repeat = 0, void* privdata = nullptr);

   public:
    /* call by CoTimer's coroutine. */
    virtual void on_repeat_timer() override;

   private:
    int new_tid() { return ++m_last_timer_id; }

   protected:
    int m_last_timer_id = 0;
    std::map<TimerGrpID, Timer*> m_timers;
    std::unordered_map<int, TimerGrpID> m_ids; /* key: timer's id. */
};

}  // namespace kim

#endif  //__KIM_TIEMRS_H__