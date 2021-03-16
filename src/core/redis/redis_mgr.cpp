#include "redis_mgr.h"

#include <hiredis/hiredis.h>
#include <stdarg.h>

#include "error.h"

#define MAX_CONN_CNT 10
#define PIPELINE_CMD_CNT 100
#define TASKS_QUEUE_LIMIT 100000

namespace kim {

RedisMgr::RedisMgr(Log* log) : Logger(log) {
}

RedisMgr::~RedisMgr() {
    destory();
}

int RedisMgr::exec_cmd(const std::string& node, const std::string& cmd, redisReply** r) {
    if (node.empty() || cmd.empty()) {
        LOG_ERROR("invalid params!");
        return ERR_INVALID_PARAMS;
    }
    return send_task(node, cmd, r);
}

int RedisMgr::send_task(const std::string& node, const std::string& cmd, redisReply** r) {
    LOG_DEBUG("send redis task, node: %s, cmd: %s.", node.c_str(), cmd.c_str());

    int ret;
    task_t* task;
    co_data_t* cd;

    for (int i = 0; i < 3; i++) {
        cd = get_co_data(node);
        if (cd == nullptr) {
            LOG_ERROR("can not find conn, node: %s", node.c_str());
            return ERR_REDIS_NO_CONNCTION;
        }
        if (cd->tasks.size() > TASKS_QUEUE_LIMIT) {
            co_sleep(1000);
            continue;
        }
        break;
    }

    if (cd->tasks.size() > TASKS_QUEUE_LIMIT) {
        LOG_WARN("redis task over limit! node: %s.", node.c_str());
        return ERR_REDIS_TASKS_OVER_LIMIT;
    }

    task = new task_t;
    task->cmd = cmd;
    task->co = GetCurrThreadCo();
    cd->tasks.push(task);

    co_cond_signal(cd->cond);
    LOG_TRACE("signal redis co handler! node: %s, co: %p", node.c_str(), cd->co);
    co_yield_ct();

    ret = task->ret;
    *r = task->reply;
    SAFE_DELETE(task);
    return ret;
}

RedisMgr::co_data_t* RedisMgr::get_co_data(const std::string& node) {
    co_data_t* cd;
    co_array_data_t* ad;

    auto it = m_rds_infos.find(node);
    if (it == m_rds_infos.end()) {
        LOG_ERROR("invalid node: %s.", node.c_str());
        return nullptr;
    }

    auto itr = m_coroutines.find(node);
    if (itr == m_coroutines.end()) {
        ad = new co_array_data_t;
        ad->ri = it->second;
        m_coroutines[node] = ad;
    } else {
        ad = (co_array_data_t*)itr->second;
        if ((int)ad->coroutines.size() >= ad->ri->max_conn_cnt) {
            cd = ad->coroutines[ad->cur_idx % ad->coroutines.size()];
            if (++ad->cur_idx == (int)ad->coroutines.size()) {
                ad->cur_idx = 0;
            }
            return cd;
        }
    }

    cd = new co_data_t;
    cd->ri = it->second;
    cd->privdata = this;
    cd->cond = co_cond_alloc();

    ad->coroutines.push_back(cd);

    LOG_INFO("node: %s, co cnt: %d, max conn cnt: %d, %d",
             node.c_str(), (int)ad->coroutines.size(), ad->ri->max_conn_cnt);

    co_create(&(cd->co), nullptr, co_handle_task, cd);
    co_resume(cd->co);
    return cd;
}

void* RedisMgr::co_handle_task(void* arg) {
    co_enable_hook_sys();
    co_data_t* cd = (co_data_t*)arg;
    RedisMgr* m = (RedisMgr*)cd->privdata;
    return m->handle_task(arg);
}

void* RedisMgr::handle_task(void* arg) {
    co_data_t* cd = (co_data_t*)arg;

    for (;;) {
        if (cd->tasks.empty()) {
            LOG_TRACE("no redis task, pls wait! node: %s, co: %p",
                      cd->ri->node.c_str(), cd->co);
            co_cond_timedwait(cd->cond, -1);
            continue;
        }

        if (cd->c == nullptr) {
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

        handle_redis_cmd(cd);

        if (cd->c->err != REDIS_OK) {
            redisFree(cd->c);
            cd->c = nullptr;
        }
    }

    return 0;
}

void RedisMgr::handle_redis_cmd(co_data_t* cd) {
    int i = 0;
    int ret = REDIS_OK;
    bool is_reply_ok = true;
    task_t* task;
    std::list<task_t*> tasks;

    while (i++ < PIPELINE_CMD_CNT && !cd->tasks.empty()) {
        task = cd->tasks.front();
        cd->tasks.pop();
        LOG_DEBUG("append redis cmd: %s", task->cmd.c_str());

        ret = redisAppendCommand(cd->c, task->cmd.c_str());
        if (ret == REDIS_OK) {
            tasks.push_back(task);
        } else {
            LOG_ERROR("redis append cmd failed! cmd: %s, err: %d, node: %s, host: %s, port: %d",
                      task->cmd.c_str(), ret,
                      cd->ri->node.c_str(), cd->ri->host.c_str(), cd->ri->port);
            task->ret = ERR_REDIS_APPEND_CMD_FAILED;
            co_resume(task->co);
        }
    }

    for (auto& v : tasks) {
        ret = redisGetReply(cd->c, (void**)&v->reply);
        if (ret != REDIS_OK || cd->c->err != REDIS_OK) {
            is_reply_ok = false;
            LOG_ERROR("redis get reply failed! err: %d, errstr: %s, node: %s, host: %s, port: %d",
                      cd->c->err, cd->c->errstr,
                      cd->ri->node.c_str(), cd->ri->host.c_str(), cd->ri->port);
        } else if ((v->reply != nullptr) && (v->reply->type == REDIS_REPLY_ERROR)) {
            is_reply_ok = false;
            LOG_ERROR("redis get reply failed! err: %d, errstr: %s, node: %s, host: %s, port: %d",
                      v->reply->type, v->reply->str,
                      cd->ri->node.c_str(), cd->ri->host.c_str(), cd->ri->port);
        }

        if (!is_reply_ok) {
            freeReplyObject(v->reply);
            v->reply = nullptr;
            v->ret = ERR_REDIS_GET_REPLY_FAILED;
        }
    }

    for (auto& v : tasks) {
        co_resume(v->co);
    }
}

void RedisMgr::clear_co_tasks(co_data_t* cd) {
    task_t* task;

    while (!cd->tasks.empty()) {
        task = cd->tasks.front();
        cd->tasks.pop();
        task->ret = ERR_REDIS_TASKS_CLEAR;
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
            while (!v->tasks.empty()) {
                SAFE_DELETE(v->tasks.front());
                v->tasks.pop();
            }
        }
    }
    m_coroutines.clear();
}

}  // namespace kim
