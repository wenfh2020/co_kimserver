#include <hiredis/hiredis.h>

#include "../common/common.h"

#define REDIS_PORT 6379
#define REDIS_HOST "127.0.0.1"

void* co_handler(void* arg) {
    co_enable_hook_sys();

    redisReply* r;
    redisContext* c;
    long long start;

    start = mstime();
    c = redisConnect(REDIS_HOST, REDIS_PORT);
    if (c == nullptr) {
        printf("conn err: can not alloc redis context\n");
        return 0;
    } else if (c->err) {
        printf("connect err: %s\n", c->errstr);
        redisFree(c);
        return 0;
    }

    for (int i = 0; i < 10; i++) {
        // r = (redisReply*)redisCommand(c, "lpush msg_list %d", i);
        r = (redisReply*)redisCommand(c, "set key111 value111");
        if (r->type == REDIS_REPLY_ERROR) {
            printf("reply error, type: %d, str: %s\n", r->type, r->str);
            break;
        }
        printf("reply data, type: %d, str: %s\n", r->type, r->str);
        freeReplyObject(r);
    }

    for (;;) {
        struct pollfd pf = {0};
        pf.fd = -1;
        poll(&pf, 1, 1000);
    }

    printf("interval: %llu\n", mstime() - start);
    redisFree(c);
    return 0;
}

int main() {
    for (int i = 0; i < 2; i++) {
        stCoRoutine_t* co;
        co_create(&co, nullptr, co_handler, nullptr);
        co_resume(co);
    }
    co_eventloop(co_get_epoll_ct(), 0, 0);
    return 0;
}