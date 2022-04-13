#include "session.h"

#define FREE_CO_CNT 5000

namespace kim {

// Session
////////////////////////////////////////////////
Session::Session(std::shared_ptr<Log> logger, std::shared_ptr<INet> net, const std::string& id)
    : Logger(logger), Net(net), m_sessid(id) {
}

// SessionMgr
////////////////////////////////////////////////

SessionMgr::SessionMgr(std::shared_ptr<Log> logger, std::shared_ptr<INet> net) : Logger(logger), Net(net) {
}

bool SessionMgr::init() {
    m_timers = std::make_shared<Timers>(logger());
    if (!m_timers->init_timer()) {
        return false;
    }
    return true;
}

bool SessionMgr::add_session(std::shared_ptr<Session> session, uint64_t after, uint64_t repeat) {
    if (session == nullptr) {
        return false;
    }

    LOG_TRACE("add session, sessid: %s", session->id());

    if (after == 0) {
        LOG_ERROR("invalid after time.");
        return false;
    }

    auto it = m_sessions.find(session->id());
    if (it != m_sessions.end()) {
        LOG_ERROR("duplicate session. sessid: %s", session->id());
        return false;
    }

    auto timer = add_timer(session, after, repeat);
    if (timer == nullptr) {
        return false;
    }

    m_sessions[session->id()] = timer;
    LOG_DEBUG("add session done, sessid: %s, after: %llu, repeat: %llu",
              session->id(), after, repeat);
    return true;
}

std::shared_ptr<Session> SessionMgr::get_session(const std::string& id) {
    auto it = m_sessions.find(id);
    if (it != m_sessions.end()) {
        return it->second->session;
    }
    return nullptr;
}

bool SessionMgr::del_session(const std::string& id) {
    auto it = m_sessions.find(id);
    if (it == m_sessions.end()) {
        return false;
    }
    del_timer(it->second->timer_id);
    m_sessions.erase(it);
    LOG_DEBUG("delete session done! sessid: %s", id.c_str());
    return true;
}

std::shared_ptr<SessionMgr::tm_session_t>
SessionMgr::add_timer(std::shared_ptr<Session> session, uint64_t after, uint64_t repeat) {
    auto self = std::weak_ptr<SessionMgr>(shared_from_this());
    auto timer = std::make_shared<tm_session_t>();
    timer->session = session;

    auto timer_id = m_timers->add_timer(
        [this, timer, self](int timer_id, bool is_repeat, void* privdata) {
            timer->session->on_timeout();
            if (!is_repeat) {
                auto p = self.lock();
                if (p == nullptr) {
                    return;
                }
                LOG_DEBUG("hit timeout callback, timer_id: %d", timer_id);
                del_session(timer->session->id());
            }
        },
        after, repeat);
    timer->timer_id = timer_id;
    return timer;
}

bool SessionMgr::del_timer(int id) {
    LOG_DEBUG("delete timer, timer id: %d", id);
    return m_timers->del_timer(id);
}

}  // namespace kim
