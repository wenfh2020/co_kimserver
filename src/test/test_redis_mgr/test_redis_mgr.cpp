#include "../common/common.h"

int g_co_cnt = 0;
int g_co_cmd_cnt = 0;
int g_cur_test_cnt = 0;
int g_cur_success_cnt = 0;
int g_cur_fail_cnt = 0;
double g_begin_time = 0.0;

bool g_is_end = false;
bool g_is_read = false;

#define LOG_LEVEL Log::LL_INFO
// #define LOG_LEVEL Log::LL_DEBUG
// #define LOG_LEVEL Log::LL_TRACE

bool load_common() {
    if (!load_logger(LOG_PATH) ||
        !load_config(CONFIG_PATH) ||
        !load_redis_mgr(m_logger, g_config["redis"])) {
        return false;
    }
    m_logger->set_level(LOG_LEVEL);
    return true;
}

void show_result(redisReply* r) {
    if (r == nullptr) {
        return;
    }

    if (r->type == REDIS_REPLY_ERROR) {
        printf("reply error, type: %d, str: %s\n", r->type, r->str);
        return;
    }
    printf("reply data, type: %d, str: %s\n", r->type, r->str);
}

void* co_handler() {
    co_enable_hook_sys();

    int i, ret;
    double spend;
    char cmd[256];
    redisReply* reply = nullptr;
    const char* node = "test";
    const char* key = "key111";
    const char* val = "value111";

    for (i = 0; i < g_co_cmd_cnt; i++) {
        if (g_is_read) {
            snprintf(cmd, sizeof(cmd), "get %s", key);
        } else {
            snprintf(cmd, sizeof(cmd), "set %s %s:%d", key, val, i);
        }

        ret = g_redis_mgr->exec_cmd(node, cmd, &reply);

        g_cur_test_cnt++;
        if (ret == ERR_OK) {
            g_cur_success_cnt++;
            // printf("redis oper failed! node: %s, cmd: %s\n", node, cmd);
        } else {
            g_cur_fail_cnt++;
            co_sleep(1);
            printf("ret: %d----\n", ret);
        }
        // show_result(reply);
        freeReplyObject(reply);
    }

    if (!g_is_end && g_cur_test_cnt == g_co_cnt * g_co_cmd_cnt) {
        g_is_end = true;
        spend = time_now() - g_begin_time;
        printf("total cnt: %d, total time: %lf, avg: %lf\nsuccess cnt: %d, fail cnt: %d\n",
               g_cur_test_cnt, spend, (g_cur_test_cnt / spend),
               g_cur_success_cnt, g_cur_fail_cnt);
    }

    return 0;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        /* ./test_redis_mgr r 1 1 */
        printf("pls: ./test_redis_mgr [read/write] [co_cnt] [co_cmd_cnt]\n");
        return -1;
    }

    g_is_read = !strcasecmp(argv[1], "r");
    g_co_cnt = atoi(argv[2]);
    g_co_cmd_cnt = atoi(argv[3]);
    g_begin_time = time_now();

    if (!load_common()) {
        std::cerr << "load common fail!" << std::endl;
        return -1;
    }

    for (int i = 0; i < g_co_cnt; i++) {
        stCoRoutine_t* co;
        co_create(&co, nullptr, [](void*) { co_handler(); });
        co_resume(co);
    }

    co_eventloop(co_get_epoll_ct());
    return 0;
}