#include "./libco/co_routine.h"
#include "mysql/db_mgr.h"
#include "server.h"

using namespace kim;

int g_co_cnt = 0;
int g_co_query_cnt = 0;
int g_cur_test_cnt = 0;
bool g_is_read = false;
double g_begin_time = 0.0;

Log* m_logger = nullptr;
DBMgr* g_db_mgr = nullptr;
CJsonObject g_config;
bool g_end = false;

char sql[1024];

typedef struct co_task_s {
    int id;
    stCoRoutine_t* co;
} test_co_task_t;

std::list<test_co_task_t*> g_coroutines;

#define LOG_PATH "test.log"
#define CONFIG_PATH "../../../bin/config.json"

bool load_logger(const char* path) {
    m_logger = new Log;
    if (!m_logger->set_log_path(path)) {
        std::cerr << "set log path failed!" << std::endl;
        return false;
    }
    m_logger->set_level(Log::LL_INFO);
    m_logger->set_worker_index(0);
    m_logger->set_process_type(true);
    return true;
}

bool load_db_mgr(Log* logger, CJsonObject& config) {
    g_db_mgr = new DBMgr(logger);
    if (!g_db_mgr->init(config)) {
        LOG_ERROR("load db mgr failed!");
    }
    return true;
}

bool load_config(const std::string& path) {
    if (!g_config.Load(path)) {
        LOG_ERROR("load config failed!");
        return false;
    }
    return true;
}

bool load_common() {
    if (!load_logger(LOG_PATH) || !load_config(CONFIG_PATH) ||
        !load_db_mgr(m_logger, g_config["database"])) {
        return false;
    }
    return true;
}

void destory() {
    SAFE_FREE(m_logger);
    SAFE_FREE(g_db_mgr);
    for (auto& it : g_coroutines) {
        free(it);
    }
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
    vec_row_t* rows = new vec_row_t;
    test_co_task_t* task;
    double begin, spend;

    task = (test_co_task_t*)arg;
    begin = time_now();

    for (i = 0; i < g_co_query_cnt; i++) {
        g_cur_test_cnt++;
        if (g_is_read) {
            snprintf(sql, sizeof(sql), "select value from mytest.test_async_mysql where id = 1;");
            ret = g_db_mgr->sql_read("test", sql, *rows);
            show_mysql_res(*rows);
        } else {
            snprintf(sql, sizeof(sql),
                     "insert into mytest.test_async_mysql (value) values ('%s %d');", "hello world", i);
            ret = g_db_mgr->sql_write("test", sql);
        }
        if (ret != 0) {
            LOG_ERROR("sql read failed! node: %s, sql: %s", "test", sql);
            return 0;
        }
    }

    // spend = time_now() - begin;
    // printf("id: %d, test cnt: %d, cur spend time: %lf\n",
    //        task->id, g_co_query_cnt, spend);

    if (g_cur_test_cnt == g_co_cnt * g_co_query_cnt && !g_end) {
        g_end = true;
        spend = time_now() - g_begin_time;
        printf("total cnt: %d, total time: %lf, avg: %lf\n",
               g_cur_test_cnt, spend, (g_cur_test_cnt / spend));
    }

    return 0;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        printf("pls: ./test_libco [read/write] [co_cnt] [co_query_cnt]\n");
        return -1;
    }

    int i;
    test_co_task_t* task;

    g_is_read = !strcasecmp(argv[1], "r");
    g_co_cnt = atoi(argv[2]);
    g_co_query_cnt = atoi(argv[3]);
    g_begin_time = time_now();

    if (!load_common()) {
        std::cerr << "load common fail!" << std::endl;
        return -1;
    }

    stCoRoutineAttr_t attr;
    attr.share_stack = co_alloc_sharestack(16, 16 * 1024 * 1024);
    attr.stack_size = 0;

    for (i = 0; i < g_co_cnt; i++) {
        task = (test_co_task_t*)calloc(1, sizeof(test_co_task_t));
        task->id = i;
        task->co = nullptr;
        g_coroutines.push_back(task);
        co_create(&task->co, &attr, co_handler_mysql, task);
        co_resume(task->co);
    }

    co_eventloop(co_get_epoll_ct(), 0, 0);
    destory();
    return 0;
}
