#include "redis_mgr.h"

#include <stdarg.h>

#include "error.h"
#include "util/hash.h"

#define DEF_CONN_CNT 5
#define MAX_CONN_CNT 30

namespace kim {

RedisMgr::RedisMgr(Log* log) : Logger(log) {
}

RedisMgr::~RedisMgr() {
    destory();
}

redisReply* RedisMgr::exec_cmd(const std::string& node, const std::string& cmd) {
    if (node.empty() || cmd.empty()) {
        LOG_ERROR("invalid params!");
        return nullptr;
    }
    return send_task(node, cmd);
}

redisReply* RedisMgr::send_task(const std::string& node, const std::string& cmd) {
    LOG_DEBUG("send redis task, node: %s, cmd: %s.", node.c_str(), cmd.c_str());

    task_t* task;
    co_data_t* cd;
    redisReply* reply;

    cd = get_co_data(node, cmd);
    if (cd == nullptr) {
        LOG_ERROR("can not find conn, node: %s", node.c_str());
        return nullptr;
    }

    task = new task_t;
    task->cmd = cmd;
    task->co = GetCurrThreadCo();
    cd->tasks.push(task);

    co_cond_signal(cd->cond);
    LOG_TRACE("signal redis co handler! node: %s, co: %p", node.c_str(), cd->co);
    co_yield_ct();

    reply = task->reply;
    SAFE_DELETE(task);
    return reply;
}

void* RedisMgr::co_handle_task(void* arg) {
    co_enable_hook_sys();

    co_data_t* cd = (co_data_t*)arg;
    cd->co = GetCurrThreadCo();
    RedisMgr* m = (RedisMgr*)cd->privdata;
    return m->handle_task(arg);
}

void* RedisMgr::handle_task(void* arg) {
    task_t* task;
    co_data_t* cd = (co_data_t*)arg;

    for (;;) {
        if (cd->tasks.empty()) {
            LOG_TRACE("no redis task, pls wait! node: %s, co: %p",
                      cd->ri->node.c_str(), cd->co);
            co_cond_timedwait(cd->cond, -1);
            continue;
        }

        while (cd->c == nullptr) {
            cd->c = connect(cd->ri->host.c_str(), cd->ri->port);
            if (cd->c == nullptr) {
                LOG_ERROR("connect redis failed! node: %s, host: %s, port: %d",
                          cd->ri->node.c_str(), cd->ri->host.c_str(), cd->ri->port);
                clear_co_tasks(cd);
                co_sleep(1000);
                continue;
            }

            LOG_INFO("connect redis server done! host: %s, port: %d",
                     cd->ri->host.c_str(), cd->ri->port);
        }

        task = cd->tasks.front();
        cd->tasks.pop();

        task->reply = (redisReply*)redisCommand(cd->c, task->cmd.c_str());
        if (task->reply != nullptr) {
            co_resume(task->co);
            continue;
        }

        LOG_ERROR("redis exec cmd failed! err: %d, errstr: %s, node: %s, host: %s, port: %d",
                  cd->c->err, cd->c->errstr,
                  cd->ri->node.c_str(), cd->ri->host.c_str(), cd->ri->port);

        /* no need to reconnect. */
        if (cd->c->err != REDIS_ERR_EOF && cd->c->err != REDIS_ERR_IO) {
            co_resume(task->co);
            continue;
        }

        /* reconnect. */
        while (redisReconnect(cd->c) != REDIS_OK) {
            LOG_ERROR("redis reconnect failed! err: %d, errstr: %s, node: %s, host: %s, port: %d",
                      cd->c->err, cd->c->errstr,
                      cd->ri->node.c_str(), cd->ri->host.c_str(), cd->ri->port);
            clear_co_tasks(cd);
            co_sleep(1000);
            continue;
        }

        LOG_INFO("redis reconnect done! node: %s, host: %s, port: %d",
                 cd->ri->node.c_str(), cd->ri->host.c_str(), cd->ri->port);

        if (task != nullptr) {
            co_resume(task->co);
            task = nullptr;
        }
    }

    return 0;
}

RedisMgr::co_data_t* RedisMgr::get_co_data(const std::string& node, const std::string& obj) {
    int hash;
    co_data_t* cd;
    co_array_data_t* co_arr_data;

    auto it = m_rds_infos.find(node);
    if (it == m_rds_infos.end()) {
        LOG_ERROR("invalid node: %s.", node.c_str());
        return nullptr;
    }

    auto itr = m_coroutines.find(node);
    if (itr == m_coroutines.end()) {
        co_arr_data = new co_array_data_t;
        co_arr_data->ri = it->second;
        m_coroutines[node] = co_arr_data;
    } else {
        co_arr_data = (co_array_data_t*)itr->second;
        if ((int)co_arr_data->coroutines.size() >= co_arr_data->ri->max_conn_cnt) {
            hash = hash_fnv1_64(obj.c_str(), obj.size());
            cd = co_arr_data->coroutines[hash % co_arr_data->coroutines.size()];
            return cd;
        }
    }

    cd = new co_data_t;
    cd->ri = it->second;
    cd->privdata = this;
    cd->cond = co_cond_alloc();

    co_arr_data->coroutines.push_back(cd);

    LOG_INFO("node: %s, co cnt: %d, max conn cnt: %d, %d",
             node.c_str(), (int)co_arr_data->coroutines.size(), co_arr_data->ri->max_conn_cnt);

    co_create(&(cd->co), nullptr, co_handle_task, cd);
    co_resume(cd->co);
    return cd;
}

void RedisMgr::clear_co_tasks(co_data_t* cd) {
    task_t* task;

    while (!cd->tasks.empty()) {
        task = cd->tasks.front();
        cd->tasks.pop();
        co_resume(task->co);
    }
}

bool RedisMgr::init(CJsonObject* config) {
    if (config == nullptr) {
        LOG_ERROR("invalid params!");
        return false;
    }

    redis_info_t* ri = nullptr;
    std::vector<std::string> vec;

    config->GetKeys(vec);
    if (vec.size() == 0) {
        LOG_ERROR("database info is empty.");
        return false;
    }

    for (const auto& v : vec) {
        const CJsonObject& obj = (*config)[v];

        ri = new redis_info_t;
        ri->node = v;
        ri->host = obj("host");
        ri->port = str_to_int(obj("port"));
        ri->max_conn_cnt = str_to_int(obj("max_conn_cnt"));
        if (ri->max_conn_cnt == 0) {
            LOG_ERROR("invalid redis max conn cnt! node: %s", v.c_str());
            goto error;
        }

        if (ri->max_conn_cnt > MAX_CONN_CNT) {
            LOG_WARN("redis max conn cnt is too large! cnt: %d", ri->max_conn_cnt);
            ri->max_conn_cnt = MAX_CONN_CNT;
        }

        if (ri->host.empty() || ri->port == 0) {
            LOG_ERROR("invalid ri node info: %s", v.c_str());
            goto error;
        }

        m_rds_infos[v] = ri;
        LOG_INFO("init node info, node: %s, host: %s, port: %d, max_conn_cnt: %d",
                 ri->node.c_str(), ri->host.c_str(), ri->port, ri->max_conn_cnt);
    }

    return true;

error:
    SAFE_DELETE(ri);
    destory();
    return false;
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
        }
        LOG_ERROR("redis conn error: can't allocate redis context.");
        return nullptr;
    }

    LOG_INFO("redis connect done! conn: %p, host: %s, port: %d", c, host.c_str(), port);
    return c;
}

void RedisMgr::destory() {
    for (auto& it : m_rds_infos) {
        SAFE_DELETE(it.second);
    }
    m_rds_infos.clear();

    for (auto& it : m_coroutines) {
        co_array_data_t* d = it.second;
        for (auto& v : d->coroutines) {
            redisFree(v->c);
            co_release(v->co);
            co_cond_free(v->cond);
            SAFE_DELETE(v->ri);
            while (!v->tasks.empty()) {
                SAFE_DELETE(v->tasks.front());
                v->tasks.pop();
            }
        }
    }
    m_coroutines.clear();
}

void RedisMgr::co_sleep(int ms) {
    struct pollfd pf = {0};
    pf.fd = -1;
    poll(&pf, 1, ms);
}

}  // namespace kim