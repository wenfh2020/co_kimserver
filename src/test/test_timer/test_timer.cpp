
#include "./libco/co_routine.h"
#include "server.h"

/* 
int ret = co_epoll_wait(ctx->iEpollFd, result, stCoEpoll_t::_EPOLL_SIZE, 1);

// linux
// https://linux.die.net/man/2/epoll_wait
The timeout argument specifies the minimum number of milliseconds

// macos
int co_epoll_wait(int epfd, struct co_epoll_res *events, int maxevents, int timeout) {
    struct timespec t = {0};
    if (timeout > 0) {
        // default wait 1 second in macos.
        t.tv_sec = timeout;
    }
    ...
}
*/

void* co_handle_timer(void* arg) {
    co_enable_hook_sys();

    for (;;) {
        co_sleep(1000);
    }

    return 0;
}

int main() {
    stCoRoutine_t* co;
    co_create(&co, NULL, co_handle_timer, nullptr);
    co_resume(co);
    co_eventloop(co_get_epoll_ct(), 0, 0);
    return 0;
}