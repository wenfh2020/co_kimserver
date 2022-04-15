#include <memory>

#include "../common/common.h"

int g_co_cnt = 0;
int g_co_query_cnt = 0;
int g_cur_test_cnt = 0;
double g_begin_time = 0.0;

bool g_is_end = false;
bool g_is_read = false;

int is_end() { return g_is_end ? -1 : 0; }

bool load_common() {
    if (!load_logger(LOG_PATH) ||
        !load_config(CONFIG_PATH) ||
        !load_mysql_mgr(m_logger, g_config["database"])) {
        return false;
    }
    return true;
}

void show_mysql_result(std::shared_ptr<VecMapRow> rows) {
    for (size_t i = 0; i < rows->size(); i++) {
        auto& cols = rows->at(i);
        for (const auto& it : cols) {
            printf("col: %-5s, data: %s\n", it.first.c_str(), it.second.c_str());
            // LOG_DEBUG("col: %s, data: %s", it.first.c_str(), it.second.c_str());
        }
        printf("-------\n");
    }
}

void co_handler_mysql() {
    co_enable_hook_sys();

    int ret = -1;
    char sql[1024] = {0};

    for (int i = 1; i <= g_co_query_cnt; i++) {
        if (g_is_read) {
            snprintf(sql, sizeof(sql),
                     "select id, value from mytest.test_async_mysql where id = %d;",
                     g_cur_test_cnt);
            auto rows = std::make_shared<VecMapRow>();
            ret = g_mysql_mgr->sql_read("test", sql, rows);
            // show_mysql_result(rows);
        } else {
            snprintf(sql, sizeof(sql),
                     "insert into mytest.test_async_mysql (value) values ('%s %d');",
                     "hello world - ", g_cur_test_cnt);
            ret = g_mysql_mgr->sql_write("test", sql);
        }

        g_cur_test_cnt++;

        if (ret != 0) {
            LOG_ERROR("sql read failed! ret: %d,node: %s, sql: %s",
                      ret, "test", sql);
            break;
        }
    }

    if (ret != 0) {
        printf("test failed!\n");
        return;
    }

    if (!g_is_end && g_cur_test_cnt == g_co_cnt * g_co_query_cnt) {
        g_is_end = true;
        g_mysql_mgr->exit();
        auto spend = time_now() - g_begin_time;
        printf("total cnt: %d, total time: %lf, avg: %lf\n",
               g_cur_test_cnt, spend, (g_cur_test_cnt / spend));
    }
}

int main(int argc, char** argv) {
    if (argc < 4) {
        // ./test_mysql_mgr r 1 1
        // ./test_mysql_mgr w 1 1
        printf("pls: ./test_mysql_mgr [read/write] [co_cnt] [co_query_cnt]\n");
        return -1;
    }

    g_is_read = !strcasecmp(argv[1], "r");
    g_co_cnt = atoi(argv[2]);
    g_co_query_cnt = atoi(argv[3]);
    g_begin_time = time_now();

    if (!load_common()) {
        std::cerr << "load common fail!" << std::endl;
        return -1;
    }

    stCoRoutine_t* co = nullptr;
    for (int i = 0; i < g_co_cnt; i++) {
        co_create(&co, nullptr, [](void*) { co_handler_mysql(); });
        co_resume(co);
    }

    co_eventloop(co_get_epoll_ct(), [](void*) { return is_end(); });
    return 0;
}