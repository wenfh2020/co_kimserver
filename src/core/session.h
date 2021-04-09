#ifndef __KIM_SESSION_H__
#define __KIM_SESSION_H__

#include "libco/co_routine.h"
#include "libco/co_routine_inner.h"
#include "net.h"
#include "server.h"
#include "timers.h"
#include "util/reference.h"

namespace kim {

// Session
////////////////////////////////////////////////

class Session : public Logger, public Net, public Reference {
   public:
    Session(Log* logger, INet* net, const std::string& sessid);
    virtual ~Session() {}

    const char* sessid() { return m_sessid.c_str(); }
    const std::string& sessid() const { return m_sessid; }

    void* privdata() { return m_privdata; }
    void set_privdata(void* privdata) { m_privdata = privdata; }

   public:
    virtual void on_timeout() {}

   protected:
    std::string m_sessid;
    void* m_privdata = nullptr;
};

// SessionMgr
////////////////////////////////////////////////

class SessionMgr : public Logger, public Net, public TimerCron {
   public:
    typedef struct co_timer_s {
        int tid; /* timer id. */
        Session* s;
        void* privdata;
    } co_timer_t;

    SessionMgr(Log* logger, INet* net);
    virtual ~SessionMgr();

    bool init();
    bool del_session(const std::string& sessid);
    Session* get_session(const std::string& sessid);
    bool add_session(Session* s, uint64_t after, uint64_t repeat = 0);

    template <typename T>
    T* get_alloc_session(const std::string& sessid, uint64_t after = SESSION_TIMEOUT_VAL, uint64_t repeat = 0) {
        auto s = dynamic_cast<T*>(get_session(sessid));
        if (s == nullptr) {
            s = new T(m_logger, m_net, sessid);
            if (!add_session(s, after, repeat)) {
                SAFE_DELETE(s);
                return nullptr;
            }
        }
        return s;
    }

   private:
    void destory();

    bool del_timer(int tid);
    co_timer_t* add_timer(Session* s, uint64_t after, uint64_t repeat);

    void on_handle_session_timer(int tid, bool is_repeat, void* privdata);
    static void handle_session_timer(int tid, bool is_repeat, void* privdata);

   public:
    virtual void on_repeat_timer() override; /* call by parent, 10 times/s on Linux. */

   private:
    Timers* m_timers;
    std::queue<Session*> m_free_sessions;
    std::unordered_map<std::string, co_timer_t*> m_sessions;
};

}  // namespace kim

#endif  //__KIM_SESSION_H__
