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

    for (auto& v : m_all_coroutines) {
        redisFree(v->c);
        co_release(v->co);
        co_cond_free(v->cond);
        SAFE_DELETE(v->rds);
        while (!v->tasks.empty()) {
            SAFE_DELETE(v->tasks.front());
            v->tasks.pop();
        }
    }
    m_all_coroutines.clear();
    m_valid_coroutines.clear();
}

/* 
config json: 
{"redis":{"test":{"host":"127.0.0.1","port":6379,"max_conn_cnt":1}}}
*/
bool RedisMgr::init(CJsonObject& config) {
    redis_info_t* rds = nullptr;
    rds_co_data_t* rds_co = nullptr;
    std::vector<std::string> vec;

    srand((unsigned)time(NULL));

    config.GetKeys(vec);
    if (vec.size() == 0) {
        LOG_ERROR("database info is empty.");
        return false;
    }

    for (const auto& v : vec) {
        const CJsonObject& obj = config[v];

        rds = new redis_info_t;
        rds->node = v;
        rds->host = obj("host");
        rds->port = str_to_int(obj("port"));
        rds->max_conn_cnt = str_to_int(obj("max_conn_cnt"));

        LOG_DEBUG("max client cnt: %d", rds->max_conn_cnt);

        if (rds->max_conn_cnt == 0) {
            LOG_ERROR("invalid redis max conn cnt! node: %s", v.c_str());
            SAFE_DELETE(rds);
            destory();
            return false;
        }

        if (rds->max_conn_cnt > MAX_CONN_CNT) {
            LOG_WARN("redis max conn cnt is too large! cnt: %d", rds->max_conn_cnt);
            rds->max_conn_cnt = MAX_CONN_CNT;
        }

        if (rds->host.empty() || rds->port == 0) {
            LOG_ERROR("invalid rds node info: %s", v.c_str());
            SAFE_DELETE(rds);
            destory();
            return false;
        }

        m_rds_infos[v] = rds;
        LOG_INFO("init node info, node: %s, host: %s, port: %d, max_conn_cnt: %d",
                 rds->node.c_str(), rds->host.c_str(), rds->port, rds->max_conn_cnt);
    }

    /* each coroutine manages a redis connection. */
    for (auto it : m_rds_infos) {
        rds = it.second;
        std::vector<rds_co_data_t*> datas;

        for (int i = 0; i < rds->max_conn_cnt; i++) {
            rds_co = new rds_co_data_t;
            rds_co->rds = rds;
            rds_co->privdata = this;
            rds_co->cond = co_cond_alloc();

            datas.push_back(rds_co);
            m_all_coroutines.push_back(rds_co);

            co_create(&(rds_co->co), nullptr, co_handle_task, rds_co);
            co_resume(rds_co->co);
        }
        m_valid_coroutines[it.first] = datas;
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
    if (c == nullptr || c->err) {
        if (c != nullptr) {
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
    LOG_DEBUG("send redis task, node: %s, cmd: %s.", node.c_str(), cmd.c_str());

    auto it = m_rds_infos.find(node);
    if (it == m_rds_infos.end()) {
        LOG_ERROR("can not find node: %s.", node.c_str());
        return nullptr;
    }

    auto itr = m_valid_coroutines.find(node);
    if (itr == m_valid_coroutines.end() || itr->second.empty()) {
        LOG_ERROR("can not find redis conn! node: %s", node.c_str());
        return nullptr;
    }

    task_t* task;
    redisReply* reply;
    rds_co_data_t* co_data;

    task = new task_t;
    task->co = GetCurrThreadCo();
    task->cmd = cmd;

    std::vector<rds_co_data_t*>& datas = itr->second;
    co_data = datas[rand() % datas.size()];
    co_data->tasks.push(task);

    co_cond_signal(co_data->cond);
    LOG_TRACE("signal co handler! node: %s, co: %p", node.c_str(), co_data->co);
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

void RedisMgr::wait_connect(rds_co_data_t* rds_co) {
    if (rds_co->tasks.empty()) {
        co_cond_timedwait(rds_co->cond, -1);
    }

    task_t* task = nullptr;

    while (rds_co->c == nullptr) {
        rds_co->c = connect(rds_co->rds->host.c_str(), rds_co->rds->port);
        if (rds_co->c != nullptr) {
            add_valid_connect(rds_co);
            LOG_INFO("redis connect done! node: %s, host: %s, port: %d",
                     rds_co->rds->node.c_str(),
                     rds_co->rds->host.c_str(), rds_co->rds->port);
            return;
        }

        del_valid_connect(rds_co);
        LOG_ERROR("connect redis failed! node: %s, host: %s, port: %d",
                  rds_co->rds->node.c_str(),
                  rds_co->rds->host.c_str(), rds_co->rds->port);

        /* clear tasks. */
        while (!rds_co->tasks.empty()) {
            task = rds_co->tasks.front();
            rds_co->tasks.pop();
            co_resume(task->co);
            task = nullptr;
        }

        co_sleep(1000);
        continue;
    }
}

void* RedisMgr::handle_task(void* arg) {
    task_t* task;
    rds_co_data_t* rds_co = (rds_co_data_t*)arg;

    wait_connect(rds_co);

    LOG_INFO("connect redis server done! host: %s, port: %d",
             rds_co->rds->host.c_str(), rds_co->rds->port);

    for (;;) {
        if (rds_co->tasks.empty()) {
            LOG_TRACE("no task, pls wait! node: %s, co: %p",
                      rds_co->rds->node.c_str(), rds_co->co);
            co_cond_timedwait(rds_co->cond, -1);
            continue;
        }

        task = rds_co->tasks.front();
        rds_co->tasks.pop();

        task->reply = (redisReply*)redisCommand(rds_co->c, task->cmd.c_str());
        if (task->reply != nullptr) {
            co_resume(task->co);
            continue;
        }

        LOG_ERROR("redis exec cmd failed! err: %d, errstr: %s, node: %s, host: %s, port: %d",
                  rds_co->c->err, rds_co->c->errstr,
                  rds_co->rds->node.c_str(), rds_co->rds->host.c_str(),
                  rds_co->rds->port);

        /* no need to reconnect. */
        if (rds_co->c->err != REDIS_ERR_EOF && rds_co->c->err != REDIS_ERR_IO) {
            co_resume(task->co);
            continue;
        }

        del_valid_connect(rds_co);

        /* reconnect。 */
        while (redisReconnect(rds_co->c) != REDIS_OK) {
            LOG_ERROR("redis reconnect failed! err: %d, errstr: %s, node: %s, host: %s, port: %d",
                      rds_co->c->err, rds_co->c->errstr,
                      rds_co->rds->node.c_str(), rds_co->rds->host.c_str(),
                      rds_co->rds->port);
            /* clear tasks. */
            if (task != nullptr) {
                co_resume(task->co);
                task = nullptr;
            }
            while (!rds_co->tasks.empty()) {
                task = rds_co->tasks.front();
                rds_co->tasks.pop();
                co_resume(task->co);
                task = nullptr;
            }
            co_sleep(1000);
            continue;
        }

        add_valid_connect(rds_co);
        LOG_INFO("redis reconnect done! node: %s, host: %s, port: %d",
                 rds_co->rds->node.c_str(), rds_co->rds->host.c_str(),
                 rds_co->rds->port);

        if (task != nullptr) {
            co_resume(task->co);
            task = nullptr;
        }
    }

    return 0;
}

bool RedisMgr::add_valid_connect(rds_co_data_t* rds_co) {
    auto it = m_valid_coroutines.find(rds_co->rds->node);
    if (it == m_valid_coroutines.end()) {
        return false;
    }

    std::vector<rds_co_data_t*>& vec = it->second;
    auto itr = vec.begin();
    for (; itr != vec.end(); itr++) {
        if (*itr == rds_co) {
            return true;
        }
    }

    vec.push_back(rds_co);
    return true;
}

bool RedisMgr::del_valid_connect(rds_co_data_t* rds_co) {
    auto it = m_valid_coroutines.find(rds_co->rds->node);
    if (it == m_valid_coroutines.end() || it->second.empty()) {
        return false;
    }

    std::vector<rds_co_data_t*>& vec = it->second;
    auto itr = vec.begin();
    for (; itr != vec.end(); itr++) {
        if (*itr == rds_co) {
            vec.erase(itr);
            return true;
        }
    }

    return false;
}

void RedisMgr::co_sleep(int ms) {
    struct pollfd pf = {0};
    pf.fd = -1;
    poll(&pf, 1, ms);
}

}  // namespace kim