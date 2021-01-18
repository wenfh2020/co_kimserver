#include "../common/common.h"

int g_co_cnt = 0;
int g_co_query_cnt = 0;
int g_cur_test_cnt = 0;
double g_begin_time = 0.0;

bool g_is_end = false;
bool g_is_read = false;

#define LOG_PATH "test.log"
#define CONFIG_PATH "../../../bin/config.json"

typedef struct co_task_s {
    int id;
    stCoRoutine_t* co;
} co_task_t;

std::list<co_task_t*> g_coroutines;

void destory() {
    SAFE_FREE(m_logger);
    SAFE_FREE(g_mysql_mgr);
    for (auto& it : g_coroutines) {
        free(it);
    }
}

bool load_common() {
    if (!load_logger(LOG_PATH) || !load_config(CONFIG_PATH) ||
        !load_mysql_mgr(m_logger, g_config["database"])) {
        return false;
    }
    return true;
}

void show_mysql_res(const vec_row_t& rows) {
    for (size_t i = 0; i < rows.size(); i++) {
        const map_row_t& items = rows[i];
        for (const auto& it : items) {
            LOG_DEBUG("col: %s, data: %s", it.first.c_str(), it.second.c_str());
        }
    }
}

void* co_handler_mysql(void* arg) {
    co_enable_hook_sys();

    int i, ret;
    vec_row_t* rows;
    // co_task_t* task;
    double spend;
    // double begin;

    rows = new vec_row_t;
    // task = (co_task_t*)arg;
    // begin = time_now();

    for (i = 0; i < g_co_query_cnt; i++) {
        char sql[1024];

        if (g_is_read) {
            snprintf(sql, sizeof(sql), "select value from mytest.test_async_mysql where id = 1;");
            ret = g_mysql_mgr->sql_read("test", sql, *rows);
            show_mysql_res(*rows);
        } else {
            snprintf(sql, sizeof(sql),
                     "insert into mytest.test_async_mysql (value) values ('%s %d');", "hello world", i);
            ret = g_mysql_mgr->sql_write("test", sql);
        }

        g_cur_test_cnt++;

        if (ret != 0) {
            LOG_ERROR("sql read failed! node: %s, sql: %s", "test", sql);
            break;
        }
    }

    // spend = time_now() - begin;
    // printf("id: %d, test cnt: %d, cur spend time: %lf\n",
    //        task->id, g_co_query_cnt, spend);
    if (ret != 0) {
        printf("test failed!\n");
        return 0;
    }

    if (g_cur_test_cnt == g_co_cnt * g_co_query_cnt && !g_is_end) {
        g_is_end = true;
        spend = time_now() - g_begin_time;
        printf("total cnt: %d, total time: %lf, avg: %lf\n",
               g_cur_test_cnt, spend, (g_cur_test_cnt / spend));
    }

    return 0;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        /* ./test_mysql_mgr r 1 1 */
        printf("pls: ./test_mysql_mgr [read/write] [co_cnt] [co_query_cnt]\n");
        return -1;
    }

    int i;
    co_task_t* task;

    g_is_read = !strcasecmp(argv[1], "r");
    g_co_cnt = atoi(argv[2]);
    g_co_query_cnt = atoi(argv[3]);
    g_begin_time = time_now();

    if (!load_common()) {
        std::cerr << "load common fail!" << std::endl;
        return -1;
    }

    for (i = 0; i < g_co_cnt; i++) {
        task = (co_task_t*)calloc(1, sizeof(co_task_t));
        task->id = i;
        task->co = nullptr;
        g_coroutines.push_back(task);
        co_create(&task->co, nullptr, co_handler_mysql, task);
        co_resume(task->co);
    }

    co_eventloop(co_get_epoll_ct(), 0, 0);
    return 0;
}
