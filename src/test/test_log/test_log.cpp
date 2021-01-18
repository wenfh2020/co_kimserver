#include <sys/time.h>

#include <iostream>

#include "./libco/co_routine.h"
#include "server.h"
#include "util/log.h"
#include "util/util.h"

#define MAX_CNT 10
kim::Log* m_logger = nullptr;

void co_sleep(int fd, int ms) {
    struct pollfd pf = {0};
    pf.fd = fd;
    poll(&pf, 1, ms);
}

void* co_handler1(void* arg) {
    co_enable_hook_sys();
    for (;;) {
        for (int i = 0; i < MAX_CNT; i++) {
            LOG_DEBUG("1 -- FDSAFHJDSFHARYEWYREW %s", "hello world!");
        }
        co_sleep(-1, 1000);
    }

    return 0;
}

void* co_handler2(void* arg) {
    co_enable_hook_sys();
    for (;;) {
        for (int i = 0; i < MAX_CNT; i++) {
            LOG_DEBUG("2 -- FDSAFHJDSFHARYEWYREW %s", "hello world!");
        }
        co_sleep(-1, 1000);
    }
    return 0;
}

int main() {
    m_logger = new kim::Log;
    if (!m_logger->set_log_path("./test.log")) {
        std::cerr << "set log path failed" << std::endl;
        return 0;
    }

    stCoRoutine_t* co;
    co_create(&co, NULL, co_handler1, nullptr);
    co_resume(co);
    co_create(&co, NULL, co_handler2, nullptr);
    co_resume(co);
    co_eventloop(co_get_epoll_ct(), 0, 0);

    SAFE_DELETE(m_logger);
    return 0;
}
