#include <sys/time.h>

#include <iostream>

#include "./libco/co_routine.h"
#include "server.h"
#include "util/log.h"
#include "util/util.h"

#define MAX_CNT 10
std::shared_ptr<kim::Log> m_logger = nullptr;

void co_handler1() {
    co_enable_hook_sys();
    for (;;) {
        for (int i = 0; i < MAX_CNT; i++) {
            LOG_DEBUG("1 -- FDSAFHJDSFHARYEWYREW %s", "hello world!");
        }
        co_sleep(1000);
    }
}

void co_handler2() {
    co_enable_hook_sys();
    for (;;) {
        for (int i = 0; i < MAX_CNT; i++) {
            LOG_DEBUG("2 -- FDSAFHJDSFHARYEWYREW %s", "hello world!");
        }
        co_sleep(1000);
    }
}

int main() {
    m_logger = std::make_shared<kim::Log>();
    if (!m_logger->set_log_path("./test.log")) {
        std::cerr << "set log path failed" << std::endl;
        return 0;
    }

    stCoRoutine_t* co;
    co_create(&co, NULL, [](void*) { co_handler1(); });
    co_resume(co);
    co_create(&co, NULL, [](void*) { co_handler2(); });
    co_resume(co);
    co_eventloop(co_get_epoll_ct());

    return 0;
}
