#include "coroutines.h"

#include "libco/co_routine_inner.h"

const int FREE_CO_MAX_CNT_ONCE = 1000;
const int SHARE_STACK_BLOCK_CNT = 128;
const int SHARE_STACK_BLOCK_SIZE = 4 * 1024 * 1024;

namespace kim {

Coroutines::Coroutines(std::shared_ptr<Log> logger) : Logger(logger) {
    m_co_attr.share_stack = co_alloc_sharestack(
        SHARE_STACK_BLOCK_CNT, SHARE_STACK_BLOCK_SIZE);
}

Coroutines::~Coroutines() {
    destory();
}

void Coroutines::destory() {
    clear();
    if (m_co_attr.share_stack != nullptr) {
        co_release_sharestack(m_co_attr.share_stack);
        m_co_attr.share_stack = nullptr;
    }
}

void Coroutines::clear() {
    while (!m_free_coroutines.empty()) {
        auto co = m_free_coroutines.front();
        co_release(co);
        m_free_coroutines.pop();
    }
    for (auto co : m_work_coroutines) {
        co_release(co);
    }
    m_work_coroutines.clear();
}

void Coroutines::exit_libco() {
    m_is_exit = true;
}

void Coroutines::run() {
    co_eventloop(co_get_epoll_ct(), [this](void*) {
        return m_is_exit ? -1 : 0;
    });
}

stCoRoutine_t* Coroutines::start_co(pfn_co_routine_t fn) {
    if (fn == nullptr) {
        LOG_ERROR("invalid param!");
        return false;
    }

    if ((int)m_work_coroutines.size() > m_max_co_cnt) {
        LOG_ERROR("exceed the coroutines's limit: %d", m_max_co_cnt);
        return false;
    }

    stCoRoutine_t* co = nullptr;
    if (m_free_coroutines.empty()) {
        co_create(&co, &m_co_attr, fn);
        m_work_coroutines.insert(co);
    } else {
        co = m_free_coroutines.front();
        m_free_coroutines.pop();
        co_reset(co);
        co->pfn = fn;
        LOG_TRACE("reuse free co: %p", co);
    }
    co->arg = co;
    co_resume(co);
    return co;
}

bool Coroutines::add_free_co(stCoRoutine_t* co) {
    if (co == nullptr) {
        return false;
    }

    auto it = m_work_coroutines.find(co);
    if (it == m_work_coroutines.end()) {
        return false;
    }

    LOG_TRACE("add free co: %p", co);
    co->pfn = nullptr;
    m_work_coroutines.erase(it);
    m_free_coroutines.push(co);
    return true;
}

void Coroutines::on_repeat_timer() {
    /* release free coroutines in timer. */
    run_with_period(1000) {
        int i = 0;
        while (!m_free_coroutines.empty() && i++ < FREE_CO_MAX_CNT_ONCE) {
            auto co = m_free_coroutines.front();
            m_free_coroutines.pop();
            co_release(co);
        }

        if (!m_free_coroutines.empty()) {
            LOG_INFO("free co cnt: %u", m_free_coroutines.size());
        }
    }
}

}  // namespace kim
