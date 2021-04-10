#include "coroutines.h"

#include "libco/co_routine_inner.h"

#define FREE_CO_CNT 1000

namespace kim {

Coroutines::Coroutines(Log* logger) : Logger(logger) {
    m_co_attr.share_stack = co_alloc_sharestack(128, 4 * 1024 * 1024);
}

Coroutines::~Coroutines() {
    clear_tasks();
    if (m_co_attr.share_stack != nullptr) {
        co_release_sharestack(m_co_attr.share_stack);
        m_co_attr.share_stack = nullptr;
    }
}

void Coroutines::clear_tasks() {
    while (!m_free_coroutines.empty()) {
        co_task_t* task = m_free_coroutines.front();
        m_free_coroutines.pop();
        co_release(task->co);
        SAFE_FREE(task);
    }

    for (auto& v : m_work_coroutines) {
        co_release(v->co);
        free(v);
    }
    m_work_coroutines.clear();
}

void Coroutines::exit_libco() {
    m_is_exit = true;
}

int Coroutines::co_event_loop_handler(void* d) {
    Coroutines* cos = (Coroutines*)d;
    return cos->event_loop_handler(d);
}

int Coroutines::event_loop_handler(void*) {
    return m_is_exit ? -1 : 0;
}

void Coroutines::run() {
    co_eventloop(co_get_epoll_ct(), &co_event_loop_handler, this);
}

co_task_t* Coroutines::create_co_task(Connection* c, pfn_co_routine_t fn) {
    co_task_t* task;

    if ((int)m_work_coroutines.size() > m_max_co_cnt) {
        LOG_ERROR("exceed the coroutines‘s limit: %d", m_max_co_cnt);
        return nullptr;
    }

    if (m_free_coroutines.size() == 0) {
        task = (co_task_t*)calloc(1, sizeof(co_task_t));
        task->c = c;
        m_work_coroutines.insert(task);
        co_create(&task->co, &m_co_attr, fn, (void*)task);
    } else {
        task = m_free_coroutines.front();
        m_free_coroutines.pop();
        co_reset(task->co);
        if (task->c != nullptr) {
            LOG_WARN("pls ensure connection: %p\n", c);
        }
        task->c = c;
        task->co->pfn = fn;
        LOG_DEBUG("reuse free co: %p", task->co);
    }
    return task;
}

bool Coroutines::add_free_co_task(co_task_t* task) {
    if (task == nullptr) {
        return false;
    }

    auto it = m_work_coroutines.find(task);
    if (it == m_work_coroutines.end()) {
        return false;
    }

    m_work_coroutines.erase(it);
    m_free_coroutines.push(task);
    return true;
}

void Coroutines::on_repeat_timer() {
    int i = 0;
    co_task_t* task;

    /* release free coroutines in timer. */
    run_with_period(1000) {
        while (!m_free_coroutines.empty() && i++ < FREE_CO_CNT) {
            task = m_free_coroutines.front();
            m_free_coroutines.pop();
            co_release(task->co);
            SAFE_FREE(task);
        }

        if (!m_free_coroutines.empty()) {
            LOG_INFO("free co cnt: %u", m_free_coroutines.size());
        }
    }
}

}  // namespace kim
