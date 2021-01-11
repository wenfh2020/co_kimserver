#include "coroutines.h"

#include "co_routine_inner.h"

namespace kim {

Coroutines::~Coroutines() {
    clear_tasks();
}

void Coroutines::clear_tasks() {
    for (const auto& task : m_coroutines) {
        co_release(task->co);
    }
    m_coroutines.clear();
}

co_task_t* Coroutines::create_co_task(Connection* c, pfn_co_routine_t fn) {
    co_task_t* task;

    if (m_co_free.size() == 0) {
        if (m_coroutines.size() > m_max_co_cnt) {
            LOG_ERROR("exceed the coroutines limit: %d", m_max_co_cnt);
            return nullptr;
        }
        task = (co_task_t*)calloc(1, sizeof(co_task_t));
        task->c = c;
        m_coroutines.insert(task);
        co_create(&task->co, NULL, fn, (void*)task);
    } else {
        auto it = m_co_free.begin();
        task = *it;
        if (task->c != nullptr) {
            LOG_WARN("pls ensure connection: %p\n", c);
        }
        task->c = c;
        task->co->pfn = fn;
        m_co_free.erase(it);
    }

    return task;
}

bool Coroutines::add_free_co_task(co_task_t* task) {
    auto it = m_co_free.insert(task);
    return it.second;
}

void Coroutines::co_sleep(int ms, int fd, int events) {
    struct pollfd pf = {0};
    pf.fd = fd;
    pf.events = events | POLLERR | POLLHUP;
    poll(&pf, 1, ms);
}

}  // namespace kim
