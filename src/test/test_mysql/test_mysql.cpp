#include <mysql/mysql.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include <iostream>

#include "./libco/co_routine.h"

int g_co_cnt = 0;
int g_co_query_cnt = 0;
int g_cur_test_cnt = 0;
double g_begin_time = 0.0;

typedef struct db_s {
    std::string host;
    int port;
    std::string user;
    std::string psw;
    std::string charset;
} db_t;

typedef struct task_s {
    int id;
    db_t* db;
    MYSQL* mysql;
    stCoRoutine_t* co;
} task_t;

double time_now() {
    struct timeval tv;
    gettimeofday(&tv, 0);
    return ((tv).tv_sec + (tv).tv_usec * 1e-6);
}

void show_mysql_error(MYSQL* mysql) {
    printf("error: %d, errstr: %s\n", mysql_errno(mysql), mysql_error(mysql));
}

int show_mysql_query_data(MYSQL* mysql, MYSQL_RES* res) {
    if (mysql == nullptr || res == nullptr) {
        return -1;
    }

    MYSQL_ROW row;
    MYSQL_FIELD* fields;
    int i, field_cnt, row_cnt;

    row_cnt = mysql_num_rows(res);
    fields = mysql_fetch_fields(res);
    field_cnt = mysql_num_fields(res);

    printf("----------\n");
    while ((row = mysql_fetch_row(res)) != NULL) {
        for (i = 0; i < field_cnt; i++) {
            printf("%s: %s, ", fields[i].name, (row[i] != nullptr) ? row[i] : "");
        }
        printf("\n");
    }
    printf("----------\n");

    return row_cnt;
}

void* co_handler_mysql_query(void* arg) {
    co_enable_hook_sys();

    int i;
    db_t* db;
    task_t* task;
    MYSQL_RES* res;
    const char* query;
    double begin, spend;

    task = (task_t*)arg;
    db = task->db;

    /* connect mysql */
    if (task->mysql == nullptr) {
        task->mysql = mysql_init(NULL);
        if (!mysql_real_connect(
                task->mysql,
                db->host.c_str(),
                db->user.c_str(),
                db->psw.c_str(),
                "mysql",
                db->port,
                NULL, 0)) {
            show_mysql_error(task->mysql);
            return nullptr;
        }
    }

    begin = time_now();

    for (i = 0; i < g_co_query_cnt; i++) {
        g_cur_test_cnt++;
        /* select mysql. */
        query = "select * from mytest.test_async_mysql where id = 1;";
        if (mysql_real_query(task->mysql, query, strlen(query))) {
            show_mysql_error(task->mysql);
            return nullptr;
        }

        res = mysql_store_result(task->mysql);
        // show_mysql_query_data(task->mysql, res);
        mysql_free_result(res);
    }

    spend = time_now() - begin;
    printf("id: %d, test cnt: %d, cur spend time: %lf\n",
           task->id, g_co_query_cnt, spend);

    if (g_cur_test_cnt == g_co_cnt * g_co_query_cnt) {
        spend = time_now() - g_begin_time;
        printf("total cnt: %d, total time: %lf, avg: %lf\n",
               g_cur_test_cnt, spend, (g_cur_test_cnt / spend));
    }

    /* close mysql. */
    mysql_close(task->mysql);
    delete task;
    return nullptr;
}

void* test_co(void* arg) {
    printf("hello world\n");
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("pls: ./test_libco [co_cnt] [co_query_cnt]\n");
        return -1;
    }

    int i;
    db_t* db;
    task_t* task;

    g_co_cnt = atoi(argv[1]);
    g_co_query_cnt = atoi(argv[2]);
    g_begin_time = time_now();
    db = new db_t{"127.0.0.1", 3306, "topnews", "topnews2016", "utf8mb4"};

    for (i = 0; i < g_co_cnt; i++) {
        task = new task_t{i, db, nullptr, nullptr};
        co_create(&(task->co), NULL, co_handler_mysql_query, task);
        // co_create(&(task->co), NULL, test_co, task);
        co_resume(task->co);
    }

    co_eventloop(co_get_epoll_ct(), 0, 0);
    delete db;
    return 0;
}
