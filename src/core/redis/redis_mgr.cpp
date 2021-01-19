#include "redis_mgr.h"

#include <stdarg.h>

#include "error.h"

#define DEF_CONN_CNT 5
#define MAX_CONN_CNT 30

namespace kim {

RedisMgr::RedisMgr(Log* logger) : Logger(logger) {
}

RedisMgr::~RedisMgr() {
    destory();
}

void RedisMgr::destory() {
    for (auto& it : m_rds_infos) {
        SAFE_DELETE(it.second);
    }
    m_rds_infos.clear();

    for (auto& it : m_tasks) {
        std::queue<task_t*>& que = it.second;
        while (!que.empty()) {
            SAFE_DELETE(que.front());
            que.pop();
        }
    }
    m_tasks.clear();

    co_cond_free(m_task_cond);

    for (auto& v : m_co_datas) {
        redisFree(v->c);
        co_release(v->co);
    }
    m_co_datas.clear();
}

bool RedisMgr::init(CJsonObject& config) {
    rds_co_data_t* rds_co;
    redis_info_t* rds;
    std::vector<std::string> vec;

    config.GetKeys(vec);

    if (vec.size() == 0) {
        LOG_ERROR("database info is empty.");
        return false;
    }

    for (const auto& it : vec) {
        const CJsonObject& obj = config[it];
        rds = new redis_info_t;
        rds->node = it;
        rds->host = obj("host");
        rds->port = str_to_int(obj("port"));
        rds->max_conn_cnt = str_to_int(obj("max_conn_cnt"));

        if (rds->max_conn_cnt == 0) {
            rds->max_conn_cnt = DEF_CONN_CNT;
        } else {
            if (rds->max_conn_cnt > MAX_CONN_CNT) {
                LOG_WARN("redis max conn count is too large! cnt: %d", rds->max_conn_cnt);
                rds->max_conn_cnt = MAX_CONN_CNT;
            }
        }

        LOG_DEBUG("max client cnt: %d", rds->max_conn_cnt);

        if (rds->host.empty() || rds->port == 0) {
            LOG_ERROR("invalid rds node info: %s", it.c_str());
            SAFE_DELETE(rds);
            destory();
            return false;
        }

        m_rds_infos.insert({it, rds});
        LOG_INFO("init node info, node: %s, host: %s, port: %d, max_conn_cnt: %d",
                 rds->node.c_str(), rds->host.c_str(), rds->port, rds->max_conn_cnt);
    }

    m_task_cond = co_cond_alloc();

    for (auto& it : m_rds_infos) {
        rds = it.second;
        for (int i = 0; i < rds->max_conn_cnt; i++) {
            rds_co = new rds_co_data_t;
            rds_co->rds = rds;
            rds_co->privdata = this;
            rds_co->c = nullptr;

            co_create(&(rds_co->co), nullptr, co_handle_task, rds_co);
            co_resume(rds_co->co);
            m_co_datas.insert(rds_co);
        }
    }

    LOG_INFO("init redis mgr done!");
    return true;
}

redisReply* RedisMgr::exec_cmd(const std::string& node, const std::string& cmd) {
    if (node.empty() || cmd.empty()) {
        LOG_ERROR("invalid redis params!");
        return nullptr;
    }

    return send_task(node, cmd);
}

redisContext* RedisMgr::connect(const std::string& host, int port) {
    if (host.empty() || port == 0) {
        LOG_ERROR("invalid params!");
        return nullptr;
    }

    redisContext* c = redisConnect(host.c_str(), port);
    if (c == NULL || c->err) {
        if (c) {
            LOG_ERROR("redis conn error: %s", c->errstr);
            redisFree(c);
        } else {
            LOG_ERROR("redis conn error: can't allocate redis context.");
        }
        return nullptr;
    }

    LOG_INFO("redis connect done! conn: %p, host: %s, port: %d",
             c, host.c_str(), port);
    return c;
}

redisReply* RedisMgr::send_task(const std::string& node, const std::string& cmd) {
    auto it_node = m_rds_infos.find(node);
    if (it_node == m_rds_infos.end()) {
        LOG_ERROR("can not find node: %s.", node.c_str());
        return nullptr;
    }

    LOG_DEBUG("send redis task, node: %s, cmd: %s.",
              node.c_str(), cmd.c_str());

    task_t* task;
    redisReply* reply;

    task = new task_t;
    task->co = GetCurrThreadCo();
    task->cmd = cmd;

    auto it = m_tasks.find(node);
    if (it == m_tasks.end()) {
        std::queue<task_t*> que;
        que.push(task);
        m_tasks[node] = que;
    } else {
        it->second.push(task);
    }

    co_cond_signal(m_task_cond);
    co_yield_ct();

    reply = task->reply;
    SAFE_DELETE(task);

    return reply;
}

void* RedisMgr::co_handle_task(void* arg) {
    co_enable_hook_sys();
    rds_co_data_t* rds_co = (rds_co_data_t*)arg;
    RedisMgr* m = (RedisMgr*)rds_co->privdata;
    return m->handle_task(arg);
}

void* RedisMgr::handle_task(void* arg) {
    task_t* task;
    rds_co_data_t* rds_co = (rds_co_data_t*)arg;

    while (rds_co->c == nullptr) {
        rds_co->c = connect(rds_co->rds->host.c_str(), rds_co->rds->port);
        if (rds_co->c == nullptr) {
            LOG_ERROR("connect redis failed! host: %s, port: %d",
                      rds_co->rds->host.c_str(), rds_co->rds->port);
            struct pollfd pf = {0};
            pf.fd = -1;
            poll(&pf, 1, 1000);
            continue;
        }
    }

    LOG_INFO("connect redis server done! host: %s, port: %d",
             rds_co->rds->host.c_str(), rds_co->rds->port);

    for (;;) {
        auto it = m_tasks.find(rds_co->rds->node);
        if (it == m_tasks.end() || it->second.empty()) {
            co_cond_timedwait(m_task_cond, -1);
            continue;
        }

        std::queue<task_t*>& que = it->second;
        task = que.front();
        que.pop();

        LOG_DEBUG("exec redis cmd: %s.", task->cmd.c_str());
        task->reply = (redisReply*)redisCommand(rds_co->c, task->cmd.c_str());
        LOG_DEBUG("redis cmd result: %p", task->reply);
        co_resume(task->co);
    }

    return 0;
}

}  // namespace kim