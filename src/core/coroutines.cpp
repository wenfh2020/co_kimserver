#include "coroutines.h"

#include "libco/co_routine_inner.h"

#define FREE_CO_CNT 100

namespace kim {

Coroutines::Coroutines(Log* logger) : m_logger(logger) {
    m_co_attr.share_stack = co_alloc_sharestack(64, 4 * 1024 * 1024);
}

Coroutines::~Coroutines() {
    clear_tasks();
    if (m_co_attr.share_stack != nullptr) {
        co_release_sharestack(m_co_attr.share_stack);
        m_co_attr.share_stack = nullptr;
    }
}

void Coroutines::clear_tasks() {
    for (auto& task : m_coroutines) {
        co_release(task->co);
        free(task);
    }
    m_co_free.clear();
    m_coroutines.clear();
}

void Coroutines::run() {
    co_eventloop(co_get_epoll_ct(), 0, 0);
}

co_task_t* Coroutines::create_co_task(Connection* c, pfn_co_routine_t fn) {
    co_task_t* task;

    if (m_co_free.size() == 0) {
        if ((int)m_coroutines.size() > m_max_co_cnt) {
            LOG_ERROR("exceed the coroutines‘s limit: %d", m_max_co_cnt);
            return nullptr;
        }
        task = (co_task_t*)calloc(1, sizeof(co_task_t));
        task->c = c;
        m_coroutines.insert(task);
        co_create(&task->co, &m_co_attr, fn, (void*)task);
    } else {
        task = *m_co_free.begin();
        co_reset(task->co);
        if (task->c != nullptr) {
            LOG_WARN("pls ensure connection: %p\n", c);
        }
        task->c = c;
        task->co->pfn = fn;
        m_co_free.erase(task);
        LOG_DEBUG("use free co: %p", task->co);
    }

    return task;
}

bool Coroutines::add_free_co_task(co_task_t* task) {
    if (task != nullptr) {
        if (m_coroutines.find(task) != m_coroutines.end()) {
            return m_co_free.insert(task).second;
        }
    }
    return false;
}

void Coroutines::on_repeat_timer() {
    int i = 0;
    co_task_t* task;

    /* release free coroutines in timer. */
    run_with_period(1000) {
        while (!m_co_free.empty() && i++ < FREE_CO_CNT) {
            task = *m_co_free.begin();
            co_release(task->co);
            m_co_free.erase(task);
            m_coroutines.erase(task);
            SAFE_FREE(task);
        }

        if (!m_co_free.empty()) {
            LOG_DEBUG("free co cnt: %u", m_co_free.size());
        }
    }

    m_cronloops++;
}

}  // namespace kim
