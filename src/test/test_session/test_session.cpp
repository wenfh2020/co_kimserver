#include "../common/common.h"
#include "user_session.h"

typedef struct test_task_s {
    int id;
    stCoRoutine_t* co;
} test_task_t;

std::list<test_task_t*> g_free_tasks;

void create_timer_co();
void test_session_mgr(int cnt);
bool init(int argc, char** argv);

void* co_timer(void* arg);
void* co_session(void* arg);

int main(int argc, char** argv) {
    if (!init(argc, argv)) {
        return -1;
    }
    create_timer_co();
    test_session_mgr(atoi(argv[1]));
    co_eventloop(co_get_epoll_ct(), 0, 0);
    return 0;
}

void test_session_mgr(int cnt) {
    test_task_t* task;

    for (int i = 0; i < cnt; i++) {
        task = (test_task_t*)calloc(1, sizeof(test_task_t));
        task->id = i;
        co_create(&task->co, nullptr, co_session, (void*)task);
        co_resume(task->co);
        LOG_DEBUG("create coroutine, id:%d, co: %p", task->id, task->co);
    }
}

void* co_session(void* arg) {
    co_enable_hook_sys();
    test_task_t* task = (test_task_t*)arg;

    for (;;) {
        std::string sessid(format_str("%d", task->id));
        auto sess = g_session_mgr->get_alloc_session<UserSession>(sessid);
        if (sess == nullptr) {
            LOG_ERROR("get sess failed! sessid: %s", sessid.c_str());
            break;
        }

        sess->set_user_id(str_to_int(sessid));
        sess->set_user_name(format_str("hello world - %d", task->id));
        LOG_DEBUG("session info, session id: %s, userid: %d,name: %s",
                  sess->id(), sess->user_id(), sess->user_name().c_str())

        // if (!g_session_mgr->del_session(sessid)) {
        //     LOG_ERROR("delete sess failed! sessid: %s", sessid.c_str());
        // }

        co_sleep(10 * 1000);
        break;
    }

    g_free_tasks.push_back(task);
    return 0;
}

void* co_timer(void* arg) {
    co_enable_hook_sys();

    for (;;) {
        co_sleep(100);

        for (auto& v : g_free_tasks) {
            co_release(v->co);
            SAFE_FREE(v);
        }
        g_free_tasks.clear();

#if !defined(__APPLE__)
        malloc_trim(0);
#endif
    }

    return 0;
}

bool init(int argc, char** argv) {
    if (argc != 2) {
        printf("./proc [cnt]\n");
        return false;
    }

    if (!load_logger("./test.log", Log::LL_TRACE) ||
        !load_session_mgr()) {
        return false;
    }

    return true;
}

void create_timer_co() {
    stCoRoutine_t* co;
    co_create(&co, NULL, co_timer, nullptr);
    co_resume(co);
}
