#ifndef __KIM_SESSION_H__
#define __KIM_SESSION_H__

#include "libco/co_routine.h"
#include "libco/co_routine_inner.h"
#include "net.h"
#include "server.h"
#include "timer.h"
#include "util/reference.h"

namespace kim {

// Session
////////////////////////////////////////////////

class Session : public Logger, public Reference {
   public:
    Session(Log* logger, INet* net, const std::string& sessid, uint64_t alive = SESSION_TIMEOUT_VAL);
    virtual ~Session();

    const std::string& sessid() const { return m_sessid; }
    const char* sessid() { return m_sessid.c_str(); }

    uint64_t keep_alive() { return m_keep_alive; }
    void set_keep_alive(uint64_t tm) { m_keep_alive = tm; }

    uint64_t active_time() const { return m_active_time; }
    void set_active_time(uint64_t tm) { m_active_time = tm; }

   public:
    virtual void on_timeout() {}

   protected:
    INet* m_net = nullptr;
    std::string m_sessid;       /* session id. */
    uint64_t m_keep_alive = 0;  /* session alive time. */
    uint64_t m_active_time = 0; /* session active time. */
};

// SessionMgr
////////////////////////////////////////////////

class SessionMgr : public Logger, public TimerCron {
   public:
    typedef struct co_timer_s {
        Session* s;
        stCoRoutine_t* co;
        void* privdata;
        bool is_ending;
    } co_timer_t;

    SessionMgr(Log* logger, INet* net);
    virtual ~SessionMgr();

    Session* add_session(Session* s);
    bool del_session(const std::string& sessid);
    Session* get_session(const std::string& sessid, bool re_active = false);

    template <typename T>
    T* get_alloc_session(const std::string& sessid,
                         uint64_t alive = SESSION_TIMEOUT_VAL, bool re_active = false) {
        auto s = dynamic_cast<T*>(get_session(sessid, re_active));
        if (s == nullptr) {
            s = new T(m_logger, m_net, sessid, alive);
            if (!add_session(s)) {
                SAFE_DELETE(s);
                return nullptr;
            }
        }
        LOG_DEBUG("get session id: %s", s->sessid());
        return s;
    }

   private:
    void destory();

    bool add_timer(Session* s);
    bool del_timer(const std::string& sessid);

    void on_handle_session_timer(void* arg);
    static void* co_handle_session_timer(void* arg);

   public:
    virtual void on_repeat_timer() override; /* call by parent, 10 times/s on Linux. */

   private:
    INet* m_net = nullptr;
    std::unordered_map<std::string, Session*> m_sessions;

    stCoRoutineAttr_t m_co_attr;
    std::list<co_timer_t*> m_free_timers;
    std::unordered_map<std::string, co_timer_t*> m_working_timers;
};

}  // namespace kim

#endif  //__KIM_SESSION_H__
