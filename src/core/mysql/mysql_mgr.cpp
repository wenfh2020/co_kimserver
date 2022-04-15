#include "mysql_mgr.h"

#include "error.h"

const int DEF_CONN_CNT = 5;
const int MAX_CONN_CNT = 100;
const int TASK_TIME_OUT = 10 * 1000;
const int CONN_TIME_OUT = 30 * 1000;

/* distribute tasks to connection one time. */
const int DIST_CONN_TASKS_ONCE = 50;

namespace kim {

MysqlMgr::MysqlMgr(std::shared_ptr<Log> logger) : Logger(logger) {
}

MysqlMgr::~MysqlMgr() {
    destroy();
}

int MysqlMgr::sql_write(const std::string& node, const std::string& sql) {
    if (node.empty() || sql.empty()) {
        LOG_ERROR("invalid db exec params!");
        return ERR_INVALID_PARAMS;
    }
    return send_task(node, sql, false);
}

int MysqlMgr::sql_read(const std::string& node, const std::string& sql, std::shared_ptr<VecMapRow> rows) {
    if (node.empty() || sql.empty()) {
        LOG_ERROR("invalid db query params!");
        return ERR_INVALID_PARAMS;
    }
    return send_task(node, sql, true, rows);
}

int MysqlMgr::send_task(const std::string& node, const std::string& sql,
                        bool is_read, std::shared_ptr<VecMapRow> rows) {
    LOG_DEBUG("send mysql task, node: %s, sql: %s.", node.c_str(), sql.c_str());
    if (m_is_exit) {
        LOG_WARN("db mgr is exit! node: %s", node.c_str());
        return ERR_DB_MGR_EXIT;
    }

    /* 取出任务分配器。 */
    auto md = get_co_mgr_data(node);
    if (md == nullptr) {
        LOG_ERROR("invalid node: %s", node.c_str());
        return ERR_CAN_NOT_FIND_NODE;
    }

    auto task = std::make_shared<task_t>();
    task->sql = sql;
    task->rows = rows;
    task->is_read = is_read;
    task->user_co = co_self();
    task->active_time = mstime();

    /* 先将任务放进任务分配器。 */
    md->tasks.push(task);

    /* 通知任务分配器分配任务。 */
    co_cond_signal(md->cond);
    LOG_TRACE("signal task to dist coroutines! node: %s, co: %p",
              node.c_str(), md->co);
    co_yield_ct();

    return task->ret;
}

void MysqlMgr::on_dist_task(std::shared_ptr<co_mgr_data_t> md) {
    co_enable_hook_sys();

    for (;;) {
        if (md->tasks.empty()) {
            if (m_is_exit) {
                LOG_TRACE("co func needs to exit! %p", co_self());
                break;
            } else {
                /* 如果没有任务，等待任务到来。 */
                co_cond_timedwait(md->cond, -1);
                continue;
            }
        }

        /* 取出任务处理器，分配任务。 */
        auto cd = get_co_data(md->dbi->node);
        if (cd == nullptr) {
            LOG_ERROR("can not get co data, node: %s", md->dbi->node.c_str());
            co_sleep(1000);
            continue;
        }

        /* 每个任务处理器，被分配到一定数量的任务，为了尽量分配均匀，
         * 每次每个任务处理器，最多可以分配到 DIST_CONN_TASKS_ONCE 个。 */
        for (int i = 0; i < DIST_CONN_TASKS_ONCE && !md->tasks.empty(); i++) {
            auto task = md->tasks.front();
            md->tasks.pop();
            cd->tasks.push(task);
        }

        /* 任务处理器，被分配了任务，唤醒等待的协程开始处理任务。 */
        co_cond_signal(cd->cond);
    }
}

void MysqlMgr::on_handle_task(std::shared_ptr<co_mgr_data_t> md, std::shared_ptr<co_data_t> cd) {
    co_enable_hook_sys();

    for (;;) {
        if (cd->tasks.empty()) {
            if (m_is_exit) {
                LOG_TRACE("co func needs to exit! %p", co_self());
                break;
            } else {
                // LOG_TRACE("no task, pls wait! node: %s, co: %p", cd->dbi->node.c_str(), cd->co);
                /* 如果没有任务，那将当前协程添加到空闲列表。*/
                add_to_free_list(md, cd);

                /* 如果没有任务，等待任务到来。*/
                co_cond_timedwait(cd->cond, -1);
                continue;
            }
        }

        if (cd->c == nullptr) {
            if (m_is_exit) {
                break;
            }
            cd->c = std::make_shared<MysqlConn>(logger());
            if (cd->c == nullptr || !cd->c->connect(cd->dbi)) {
                LOG_ERROR("db connect failed! node: %s, host: %s, port: %d",
                          cd->dbi->node.c_str(), cd->dbi->host.c_str(), cd->dbi->port);
                /* 如果连接失败，清空当前协程待处理的任务。 */
                clear_co_tasks(cd, ERR_DB_CONNECT_FAILED);
                co_sleep(1000);
                continue;
            }
        }

        auto task = cd->tasks.front();
        cd->tasks.pop();

        m_cur_handle_cnt++;
        cd->active_time = mstime();

        /* 带处理的任务，超时了，通知用户任务处理超时。 */
        if (cd->active_time > task->active_time + TASK_TIME_OUT) {
            LOG_WARN("task time out, sql: %s.", task->sql.c_str());
            task->ret = ERR_DB_TASKS_TIME_OUT;
            co_resume(task->user_co);
            continue;
        }

        /* 根据任务读写类型，读写数据库。 */
        if (task->is_read) {
            task->ret = cd->c->sql_read(task->sql, task->rows);
        } else {
            task->ret = cd->c->sql_write(task->sql);
        }

        auto spend = mstime() - cd->active_time;
        if (spend > m_slowlog_log_slower_than) {
            LOG_WARN("slowlog - sql spend time: %llu, sql: %s", spend, task->sql.c_str());
        }

        co_resume(task->user_co);
    }
}

/* 根据节点类型，获取对应节点的任务分配器 */
std::shared_ptr<MysqlMgr::co_mgr_data_t>
MysqlMgr::get_co_mgr_data(const std::string& node) {
    auto it = m_coroutines.find(node);
    if (it != m_coroutines.end()) {
        return it->second;
    }

    auto it_db = m_dbs.find(node);
    if (it_db == m_dbs.end()) {
        LOG_ERROR("can not find db node: %s.", node.c_str());
        return nullptr;
    }

    auto md = std::make_shared<co_mgr_data_t>();
    md->dbi = it_db->second;
    md->cond = co_cond_alloc();

    /* 创建任务分配器对应的协程。 */
    m_coroutines[node] = md;
    co_create(&(md->co), nullptr, [this, md](void*) { on_dist_task(md); });
    co_resume(md->co);
    return md;
}

std::shared_ptr<MysqlMgr::co_data_t>
MysqlMgr::get_co_data(const std::string& node) {
    auto md = get_co_mgr_data(node);
    if (md == nullptr) {
        LOG_ERROR("can not find co array data. node: %s.", node.c_str());
        return nullptr;
    }

    std::shared_ptr<co_data_t> cd = nullptr;

    /* 先从空闲队列里查询，是否有空闲的任务处理器。 */
    if (!md->free_conns.empty()) {
        cd = md->free_conns.front();
        md->free_conns.pop_front();
        md->busy_conns.push_back(cd);
    } else {
        /* 如果没有空闲任务处理器，那从繁忙队列里，从队尾获取一个任务处理器。*/
        if ((int)md->conn_cnt == md->dbi->max_conn_cnt) {
            /* get conn from busy list. */
            cd = md->busy_conns.front();
            md->busy_conns.pop_front();
            md->busy_conns.push_back(cd);
        } else {
            /* 创建一个新的任务处理器，放进繁忙队列里。 */
            cd = std::make_shared<co_data_t>();
            cd->cond = co_cond_alloc();
            cd->dbi = m_dbs.find(node)->second;

            md->conn_cnt++;
            md->busy_conns.push_back(cd);

            LOG_INFO("create new conn data, node: %s, co cnt: %d, max conn cnt: %d",
                     node.c_str(), md->conn_cnt, md->dbi->max_conn_cnt);

            co_create(&(cd->co), nullptr,
                      [this, cd, md](void*) { on_handle_task(md, cd); });
            co_resume(cd->co);
        }
    }

    return cd;
}

void MysqlMgr::on_repeat_timer() {
    co_enable_hook_sys();

    int task_cnt = 0;
    int busy_conn_cnt = 0;

    /* 每秒定时检查空闲队列，准备超时回收。 */
    run_with_period(1000) {
        for (auto& it : m_coroutines) {
            auto md = it.second;
            for (auto& cd : md->busy_conns) {
                task_cnt += cd->tasks.size();
                if (cd->c != nullptr) {
                    busy_conn_cnt++;
                }
            }
        }

        if (task_cnt > 0 || m_old_handle_cnt != m_cur_handle_cnt) {
            LOG_DEBUG("busy conn cnt: %d, avg handle cnt: %d, waiting cnt: %d",
                      busy_conn_cnt, m_cur_handle_cnt - m_old_handle_cnt, task_cnt);
            m_old_handle_cnt = m_cur_handle_cnt;
        }

        auto now = mstime();

        /* recover free connections. */
        for (auto it : m_coroutines) {
            auto md = it.second;
            for (auto cd : md->free_conns) {
                /* 回收空闲队列里，闲置超时的数据库链接。 */
                if (cd->c != nullptr && now > (cd->active_time + CONN_TIME_OUT)) {
                    LOG_INFO("recover dbi conn, node: %s, coroutines cnt: %lu, co: %p, conn: %p",
                             cd->dbi->node.c_str(), md->conn_cnt, cd->co, cd->c.get());
                    cd->c->close();
                    cd->c = nullptr;
                }
            }
        }
    }
}

void MysqlMgr::add_to_free_list(
    std::shared_ptr<co_mgr_data_t> md, std::shared_ptr<co_data_t> cd) {
    auto it = std::find(md->busy_conns.begin(), md->busy_conns.end(), cd);
    if (it != md->busy_conns.end()) {
        md->busy_conns.erase(it);
    }

    it = std::find(md->free_conns.begin(), md->free_conns.end(), cd);
    if (it == md->free_conns.end()) {
        md->free_conns.push_front(cd);
    }
}

void MysqlMgr::clear_co_tasks(std::shared_ptr<co_data_t> cd, int ret) {
    while (!cd->tasks.empty()) {
        auto task = cd->tasks.front();
        cd->tasks.pop();
        task->ret = ret;
        co_resume(task->user_co);
    }
}

bool MysqlMgr::init(CJsonObject* config) {
    if (config == nullptr) {
        LOG_ERROR("invalid params!");
        return false;
    }

    /* 从 json 取出节点数据。 */
    std::vector<std::string> nodes;
    (*config)["nodes"].GetKeys(nodes);
    if (nodes.empty()) {
        LOG_WARN("database info is empty.");
        return false;
    }

    for (const auto& node : nodes) {
        const CJsonObject& obj = (*config)["nodes"][node];

        /* 保存对应数据库节点的信息。 */
        auto dbi = std::make_shared<db_info_t>();
        dbi->host = obj("host");
        dbi->db_name = obj("name").empty() ? "mysql" : obj("name");
        dbi->password = obj("password");
        dbi->charset = obj("charset");
        dbi->user = obj("user");
        dbi->port = str_to_int(obj("port"));
        dbi->max_conn_cnt = str_to_int(obj("max_conn_cnt"));
        dbi->node = node;

        if (dbi->max_conn_cnt == 0) {
            dbi->max_conn_cnt = DEF_CONN_CNT;
        } else {
            if (dbi->max_conn_cnt > MAX_CONN_CNT) {
                LOG_WARN("db max conn count is too large! cnt: %d", dbi->max_conn_cnt);
                dbi->max_conn_cnt = MAX_CONN_CNT;
            }
        }

        LOG_DEBUG("max client cnt: %d", dbi->max_conn_cnt);

        if (dbi->host.empty() || dbi->port == 0 ||
            dbi->password.empty() || dbi->charset.empty() || dbi->user.empty()) {
            LOG_ERROR("invalid db node info: %s", node.c_str());
            return false;
        }

        m_dbs.insert({node, dbi});
    }

    /* 慢日志时间（单位：毫秒）。 */
    m_slowlog_log_slower_than = str_to_int((*config)("slowlog_log_slower_than"));
    return true;
}

void MysqlMgr::notify_exit() {
    if (m_is_exit && !m_is_notify_exit) {
        m_is_notify_exit = true;

        for (auto it : m_coroutines) {
            auto md = it.second;
            if (md->co->cEnd == 0) {
                co_cond_signal(md->cond);
            }
            for (auto cd : md->busy_conns) {
                if (cd->co->cEnd == 0) {
                    co_cond_signal(cd->cond);
                }
            }
            for (auto cd : md->free_conns) {
                if (cd->co->cEnd == 0) {
                    co_cond_signal(cd->cond);
                }
            }
        }
    }
}

void MysqlMgr::destroy() {
    /* 协程须要优雅退出（协程函数正常退出）后，协程相关结构体才能释放，
     * 否则直接释放可能会有问题。 */
    for (auto it : m_coroutines) {
        auto md = it.second;
        if (md->co->is_end()) {
            co_reset(md->co);
            co_release(md->co);
            co_cond_free(md->cond);
            md->co = nullptr;
            md->cond = nullptr;
        }

        for (auto cd : md->busy_conns) {
            if (cd->c != nullptr) {
                cd->c->close();
                cd->c = nullptr;
            }
            if (cd->co->is_end()) {
                co_reset(cd->co);
                co_release(cd->co);
                co_cond_free(cd->cond);
                cd->co = nullptr;
                cd->cond = nullptr;
            }
        }

        for (auto cd : md->free_conns) {
            if (cd->c != nullptr) {
                cd->c->close();
                cd->c = nullptr;
            }
            if (cd->co->is_end()) {
                co_reset(cd->co);
                co_release(cd->co);
                co_cond_free(cd->cond);
                cd->co = nullptr;
                cd->cond = nullptr;
            }
        }
    }
}

}  // namespace kim