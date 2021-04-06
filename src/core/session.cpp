#include "session.h"

#define FREE_CO_CNT 5000

namespace kim {

// Session
////////////////////////////////////////////////
Session::Session(Log* logger, INet* net, const std::string& sessid, uint64_t alive)
    : Logger(logger), m_net(net), m_sessid(sessid), m_keep_alive(alive) {
    set_active_time(mstime());
}

Session::~Session() {
}

// SessionMgr
////////////////////////////////////////////////

SessionMgr::SessionMgr(Log* logger, INet* net) : Logger(logger), m_net(net) {
    m_co_attr.share_stack = co_alloc_sharestack(128, 128 * 1024);
}

SessionMgr::~SessionMgr() {
    destory();
}

Session* SessionMgr::add_session(Session* s) {
    auto it = m_sessions.find(s->sessid());
    if (it != m_sessions.end()) {
        it->second->set_active_time(mstime());
        return it->second;
    }

    LOG_DEBUG("add session, sessid: %s, alive: %llu", s->sessid(), s->keep_alive());
    m_sessions[s->sessid()] = s;
    add_timer(s);
    return s;
}

Session* SessionMgr::get_session(const std::string& sessid, bool re_active) {
    auto it = m_sessions.find(sessid);
    if (it != m_sessions.end()) {
        if (re_active) {
            it->second->set_active_time(mstime());
        }
        return it->second;
    }
    return nullptr;
}

bool SessionMgr::del_session(const std::string& sessid) {
    LOG_DEBUG("delete session done! sessid: %s", sessid.c_str());

    auto it = m_sessions.find(sessid);
    if (it != m_sessions.end()) {
        m_sessions.erase(it);
        del_timer(sessid);
        return true;
    }

    return false;
}

bool SessionMgr::add_timer(Session* s) {
    co_timer_t* timer = (co_timer_t*)calloc(1, sizeof(co_timer_t));
    if (timer == nullptr) {
        return false;
    }
    timer->s = s;
    timer->privdata = this;
    timer->is_ending = false;
    m_working_timers[s->sessid()] = timer;
    co_create(&timer->co, &m_co_attr, co_handle_session_timer, (void*)timer);
    co_resume(timer->co);
    LOG_DEBUG("add timer, co: %p, sessid: %s", timer->co, timer->s->sessid());
    return true;
}

bool SessionMgr::del_timer(const std::string& sessid) {
    co_timer_t* timer;

    auto it = m_working_timers.find(sessid);
    if (it == m_working_timers.end()) {
        LOG_WARN("can not find timer, sessid: %s", it->second->s->sessid());
        return false;
    }
    m_working_timers.erase(it);

    timer = it->second;

    if (!timer->is_ending) {
        timer->is_ending = true;
        if (timer->co->cEnd == 0) {
            co_resume(timer->co);
        }
    }

    m_free_timers.push_back(timer);
    LOG_DEBUG("delete timer, sessid: %s", timer->s->sessid());
    return true;
}

void* SessionMgr::co_handle_session_timer(void* arg) {
    co_enable_hook_sys();
    co_timer_t* ct = (co_timer_t*)arg;
    SessionMgr* m = (SessionMgr*)ct->privdata;
    m->on_handle_session_timer(arg);
    return 0;
}

void SessionMgr::on_handle_session_timer(void* arg) {
    int64_t diff;
    co_timer_t* ct = (co_timer_t*)arg;

    for (;;) {
        if (ct->is_ending) {
            break;
        }

        diff = (ct->s->active_time() + ct->s->keep_alive()) - mstime();
        if (diff <= 0) {
            ct->s->on_timeout();
            LOG_TRACE("session time out! sessid: %s", ct->s->sessid());
            break;
        }
        co_sleep(diff);
    }

    ct->is_ending = true;
    del_session(ct->s->sessid());
}

void SessionMgr::on_repeat_timer() {
    int cnt = 0;
    co_timer_t* timer;

    run_with_period(1000) {
        while (!m_free_timers.empty() && cnt++ < FREE_CO_CNT) {
            timer = m_free_timers.front();
            m_free_timers.pop_front();

            if (timer->co->cEnd == 0) {
                LOG_ERROR("release working co is danger! co: %p, sessid: %s",
                          timer->co, timer->s->sessid());
            }

            if (!timer->s->is_running()) {
                LOG_DEBUG("free timer, co: %p, sessid: %s", timer->co, timer->s->sessid());
                SAFE_DELETE(timer->s);
                co_release(timer->co);
                SAFE_FREE(timer);
            } else {
                m_free_timers.push_back(timer);
                LOG_DEBUG("session is running, delay release. sessid: %s",
                          timer->s->sessid());
            }
        }

        if (!m_free_timers.empty() || !m_working_timers.empty()) {
            LOG_INFO("working session cnt: %u, free co cnt: %u",
                     m_working_timers.size(), m_free_timers.size());
        }
    }
}

void SessionMgr::destory() {
    for (auto& v : m_free_timers) {
        SAFE_DELETE(v->s);
        co_release(v->co);
        SAFE_FREE(v);
    }

    for (auto& it : m_working_timers) {
        SAFE_DELETE(it.second->s);
        co_release(it.second->co);
        SAFE_FREE(it.second);
    }

    m_sessions.clear();
    m_free_timers.clear();
    m_working_timers.clear();

    if (m_co_attr.share_stack != nullptr) {
        co_release_sharestack(m_co_attr.share_stack);
        m_co_attr.share_stack = nullptr;
    }
}

}  // namespace kim
