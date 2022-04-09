#pragma once

#include "libco/co_routine.h"
#include "libco/co_routine_inner.h"
#include "net.h"
#include "server.h"
#include "timers.h"

namespace kim {

// Session
////////////////////////////////////////////////

class Session : public Logger, public Net {
   public:
    Session(Log* logger, INet* net, const std::string& id);
    virtual ~Session() { LOG_TRACE("~Session, session id: %s", m_sessid.c_str()); }

    const char* id() { return m_sessid.c_str(); }
    const std::string& id() const { return m_sessid; }

    void* privdata() { return m_privdata; }
    void set_privdata(void* privdata) { m_privdata = privdata; }

   public:
    virtual void on_timeout() {}

   private:
    std::string m_sessid;
    void* m_privdata = nullptr;
};

// SessionMgr
////////////////////////////////////////////////

class SessionMgr : public Logger, public Net {
   public:
    typedef struct tm_session_s {
        int timer_id; /* timer id. */
        std::shared_ptr<Session> session;
        void* privdata;
    } tm_session_t;

    SessionMgr(Log* logger, INet* net);
    virtual ~SessionMgr() {}

    bool init();
    bool del_session(const std::string& id);
    std::shared_ptr<Session> get_session(const std::string& id);
    bool add_session(std::shared_ptr<Session> s, uint64_t after, uint64_t repeat = 0);

    /* add an new obj, if find by session id failed. */
    template <typename T>
    std::shared_ptr<T> get_alloc_session(
        const std::string& id, uint64_t after = SESSION_TIMEOUT_VAL, uint64_t repeat = 0) {
        std::shared_ptr<T> session = std::dynamic_pointer_cast<T>(get_session(id));
        if (session == nullptr) {
            session = std::make_shared<T>(m_logger, m_net, id);
            if (!add_session(session, after, repeat)) {
                return nullptr;
            }
        }
        return session;
    }

   private:
    bool del_timer(int id);
    std::shared_ptr<tm_session_t> add_timer(
        std::shared_ptr<Session> session, uint64_t after, uint64_t repeat);

   private:
    std::shared_ptr<Timers> m_timers;
    std::unordered_map<std::string, std::shared_ptr<tm_session_t>> m_sessions;
};

}  // namespace kim
