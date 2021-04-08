#include "../common/common.h"
#include "timers.h"

Timers* g_timers = nullptr;
std::map<int, char*> g_datas;

void test_timer1(int tid, bool is_repeat, void* privdata);
void test_timer2(int tid, bool is_repeat, void* privdata);

int main(int argc, char** argv) {
    if (argc != 2) {
        printf("./proc [timer cnt]\n");
        return -1;
    }

    if (!load_logger("./test.log", Log::LL_TRACE)) {
        return -1;
    }

    g_timers = new Timers(m_logger);
    if (!g_timers->init_timer()) {
        return -1;
    }

    int id;
    char* data;
    int cnt = atoi(argv[1]);

    /* test normal timer. */
    for (int i = 0; i < cnt; i++) {
        data = new char[64];
        snprintf(data, 64, "hello world: %d", i);
        id = g_timers->add_timer(&test_timer1, 30 * 1000, 0, data);
        g_datas[id] = data;
    }

    // for (auto& it : g_datas) {
    //     SAFE_ARRAY_DELETE(it.second);
    //     if (!g_timers->del_timer(it.first)) {
    //         printf("del timer failed! id: %d\n", it.first);
    //     }
    // }
    // g_datas.clear();

    /* test repeat timer. */
    data = new char[64];
    snprintf(data, 64, "repeat hello world");
    id = g_timers->add_timer(&test_timer2, 3 * 1000, 5 * 1000, data);
    g_datas[id] = data;

    co_eventloop(co_get_epoll_ct(), 0, 0);
    return 0;
}

void test_timer1(int tid, bool is_repeat, void* privdata) {
    if (privdata != nullptr) {
        printf("tid: %d, %s\n", tid, (char*)privdata);
        if (!is_repeat) {
            char* data = (char*)privdata;
            SAFE_ARRAY_DELETE(data);
        }
    }
}

void test_timer2(int tid, bool is_repeat, void* privdata) {
    if (privdata != nullptr) {
        printf("tid: %d, %s\n", tid, (char*)privdata);
    }
}