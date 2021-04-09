#include "session.h"

#define FREE_CO_CNT 5000

namespace kim {

// Session
////////////////////////////////////////////////
Session::Session(Log* logger, INet* net, const std::string& sessid)
    : Logger(logger), Net(net), m_sessid(sessid) {
}

// SessionMgr
////////////////////////////////////////////////

SessionMgr::SessionMgr(Log* logger, INet* net) : Logger(logger), Net(net) {
}

SessionMgr::~SessionMgr() {
    destory();
}

bool SessionMgr::init() {
    m_timers = new Timers(logger());
    if (m_timers == nullptr) {
        return false;
    }
    if (!m_timers->init_timer()) {
        SAFE_DELETE(m_timers);
        return false;
    }
    return true;
}

bool SessionMgr::add_session(Session* s, uint64_t after, uint64_t repeat) {
    if (s == nullptr) {
        return false;
    }

    LOG_TRACE("add session, sessid: %s", s->sessid());

    if (after == 0) {
        LOG_ERROR("invalid after time.");
        return false;
    }

    auto it = m_sessions.find(s->sessid());
    if (it != m_sessions.end()) {
        LOG_ERROR("duplicate session. sessid: %s", s->sessid());
        return false;
    }

    co_timer_t* ct = add_timer(s, after, repeat);
    if (ct == nullptr) {
        return false;
    }

    m_sessions[s->sessid()] = ct;
    LOG_DEBUG("add session done, sessid: %s, after: %llu, repeat: %llu",
              s->sessid(), after, repeat);
    return true;
}

Session* SessionMgr::get_session(const std::string& sessid) {
    auto it = m_sessions.find(sessid);
    if (it != m_sessions.end()) {
        return it->second->s;
    }
    return nullptr;
}

bool SessionMgr::del_session(const std::string& sessid) {
    co_timer_t* ct;

    auto it = m_sessions.find(sessid);
    if (it == m_sessions.end()) {
        return false;
    }

    ct = it->second;
    del_timer(ct->tid);
    if (ct->s->is_running()) {
        m_free_sessions.push(ct->s);
    }
    SAFE_FREE(ct);
    m_sessions.erase(it);
    LOG_DEBUG("delete session done! sessid: %s", sessid.c_str());
    return true;
}

SessionMgr::co_timer_t*
SessionMgr::add_timer(Session* s, uint64_t after, uint64_t repeat) {
    int tid;
    co_timer_t* ct;

    ct = (co_timer_t*)calloc(1, sizeof(co_timer_t));
    if (ct == nullptr) {
        return nullptr;
    }

    ct->s = s;
    ct->privdata = this;
    tid = m_timers->add_timer(&handle_session_timer, after, repeat, (void*)ct);
    ct->tid = tid;
    return ct;
}

bool SessionMgr::del_timer(int tid) {
    LOG_DEBUG("delete timer, timer id: %d", tid);
    return m_timers->del_timer(tid);
}

void SessionMgr::handle_session_timer(int tid, bool is_repeat, void* privdata) {
    co_timer_t* ct = (co_timer_t*)privdata;
    SessionMgr* m = (SessionMgr*)ct->privdata;
    return m->on_handle_session_timer(tid, is_repeat, privdata);
}

void SessionMgr::on_handle_session_timer(int tid, bool is_repeat, void* privdata) {
    co_timer_t* ct = (co_timer_t*)privdata;
    ct->s->on_timeout();
    if (!is_repeat) {
        del_session(ct->s->sessid());
    }
}

void SessionMgr::on_repeat_timer() {
    Session* s;
    int cnt = 0;
    size_t size = m_free_sessions.size();

    run_with_period(1000) {
        while (!m_free_sessions.empty() && cnt < size && cnt < FREE_CO_CNT) {
            s = m_free_sessions.front();
            m_free_sessions.pop();

            if (!s->is_running()) {
                LOG_DEBUG("free timer, sessid: %s", s->sessid());
                SAFE_DELETE(s);
            } else {
                m_free_sessions.push(s);
                LOG_DEBUG("delay release session. sessid: %s", s->sessid());
            }

            cnt++;
        }

        if (!m_free_sessions.empty() || !m_sessions.empty()) {
            LOG_TRACE("working session cnt: %u, free session cnt: %u",
                      m_sessions.size(), m_free_sessions.size());
        }
    }
}

void SessionMgr::destory() {
    while (!m_free_sessions.empty()) {
        SAFE_DELETE(m_free_sessions.front());
        m_free_sessions.pop();
    }

    for (auto& it : m_sessions) {
        SAFE_DELETE(it.second->s);
        SAFE_FREE(it.second);
    }

    m_sessions.clear();
    SAFE_DELETE(m_timers);
}

}  // namespace kim
