#include <hiredis/hiredis.h>

#include "../common/common.h"

#define REDIS_PORT 6379
#define REDIS_HOST "127.0.0.1"

int g_co_cnt = 0;
int g_co_cmd_cnt = 0;
int g_cur_test_cnt = 0;
double g_begin_time = 0.0;

bool g_is_end = false;
bool g_is_read = false;

void* co_handler(void* arg) {
    co_enable_hook_sys();

    redisReply* r;
    redisContext* c;
    double spend;
    char cmd[256];
    const char* key = "key111";
    const char* val = "value111";

    c = redisConnect(REDIS_HOST, REDIS_PORT);
    if (c == nullptr) {
        printf("conn err: can not alloc redis context\n");
        return 0;
    } else if (c->err) {
        printf("connect err: %s\n", c->errstr);
        redisFree(c);
        return 0;
    }

    for (int i = 0; i < g_co_cmd_cnt; i++) {
        if (g_is_read) {
            snprintf(cmd, sizeof(cmd), "get %s", key);
        } else {
            snprintf(cmd, sizeof(cmd), "set %s %s:%d", key, val, i);
        }

        r = (redisReply*)redisCommand(c, cmd);
        if (r == nullptr) {
            printf("exec redis cmd failed!\n");
            break;
        }
        if (r->type == REDIS_REPLY_ERROR) {
            printf("reply error, type: %d, str: %s\n", r->type, r->str);
            break;
        }

        printf("cmd: %s-----\n", cmd);
        // printf("reply data, type: %d, str: %s\n", r->type, r->str);
        freeReplyObject(r);
        g_cur_test_cnt++;
    }

    if (!g_is_end && g_cur_test_cnt == g_co_cnt * g_co_cmd_cnt) {
        g_is_end = true;
        spend = time_now() - g_begin_time;
        printf("total cnt: %d, total time: %lf, avg: %lf\n",
               g_cur_test_cnt, spend, (g_cur_test_cnt / spend));
    }

    redisFree(c);
    return 0;
}

/* ./test_hiredis r 1 1 */
int main(int argc, char** argv) {
    if (argc < 4) {
        printf("pls: ./test_hiredis [read/write] [co_cnt] [co_cmd_cnt]\n");
        return -1;
    }

    g_is_read = !strcasecmp(argv[1], "r");
    g_co_cnt = atoi(argv[2]);
    g_co_cmd_cnt = atoi(argv[3]);
    g_begin_time = time_now();

    for (int i = 0; i < g_co_cnt; i++) {
        stCoRoutine_t* co;
        co_create(&co, nullptr, co_handler, nullptr);
        co_resume(co);
    }
    co_eventloop(co_get_epoll_ct(), 0, 0);
    return 0;
}