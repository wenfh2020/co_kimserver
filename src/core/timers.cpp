#include "timers.h"

namespace kim {

// Timer
////////////////////////////////////////////////

Timer::Timer(int id, const TimerCallback& fn, uint64_t after, uint64_t repeat, void* privdata)
    : m_id(id), m_after_time(after), m_repeat_time(repeat), m_privdata(privdata), m_callback(fn) {
}

// Timers
////////////////////////////////////////////////

int Timers::add_timer(const TimerCallback& fn,
                      uint64_t after, uint64_t repeat, void* privdata) {
    int id = new_tid();
    TimerGrpID gid = {mstime() + after, id};
    auto timer = std::make_shared<Timer>(id, fn, after, repeat, privdata);

    m_timers[gid] = timer;
    m_ids[id] = gid;

    LOG_DEBUG("add timer done! id: %d", id);
    return id;
}

bool Timers::del_timer(int id) {
    auto it = m_ids.find(id);
    if (it == m_ids.end()) {
        return false;
    }

    auto it_timer = m_timers.find(it->second);
    if (it_timer != m_timers.end()) {
        m_timers.erase(it_timer);
    }
    m_ids.erase(it);

    LOG_DEBUG("delete timer done! id: %d", id);
    return true;
}

void Timers::on_repeat_timer() {
    uint64_t now = mstime();

    while (!m_timers.empty() && (m_timers.begin()->first.first < now)) {
        auto it = m_timers.begin();
        auto gid = it->first;
        auto timer = it->second;
        auto callback = timer->callback();

        m_timers.erase(it);

        if (callback != nullptr) {
            callback(timer->id(), timer->repeat_time() != 0, timer->privdata());
        }

        if (timer->repeat_time() != 0) {
            LOG_TRACE("repeat timer hit, timer id: %d, timeout: %llu, now: %llu",
                      gid.second, gid.first, now);
            TimerGrpID new_gid = {mstime() + timer->repeat_time(), gid.second};
            m_timers[new_gid] = timer;
            m_ids[gid.second] = new_gid;
        } else {
            LOG_TRACE("no repeat timer hit, delete timer, id: %d", gid.second);
            auto itr = m_ids.find(gid.first);
            if (itr != m_ids.end()) {
                m_ids.erase(itr);
            }
        }
    }

    run_with_period(1000) {
        if (!m_timers.empty() || !m_ids.empty()) {
            LOG_TRACE("timers's cnt: %lu, timer ids's cnt: %lu",
                      m_timers.size(), m_ids.size());
        }
    }
}

}  // namespace kim
