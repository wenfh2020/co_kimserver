#include "node_connection.h"

#include <netdb.h>
#include <sys/socket.h>

#include "error.h"
#include "nodes.h"
#include "sys_cmd.h"
#include "util/hash.h"
#include "util/util.h"

#define HEART_BEAT_TIME 2000
#define MAX_RECV_DATA_TIME 3000

namespace kim {

NodeConn::NodeConn(INet* net, Log* log) : Logger(log), m_net(net) {
}

NodeConn::~NodeConn() {
    destory();
}

void NodeConn::destory() {
    for (auto& it : m_coroutines) {
        node_conn_data_t* d = it.second;
        for (auto& v : d->coroutines) {
            clear_co_tasks(v);
            m_net->close_conn(v->c);
            co_cond_free(v->cond);
            co_free(v->co);
        }
        SAFE_DELETE(d);
    }
}

int NodeConn::relay_to_node(const std::string& node_type, const std::string& obj,
                            MsgHead* head_in, MsgBody* body_in, MsgHead* head_out, MsgBody* body_out) {
    if (!m_net->is_worker()) {
        LOG_ERROR("relay_to_node only for worker!");
        return ERR_INVALID_PROCESS_TYPE;
    }

    int ret;
    task_t* task;
    co_data_t* cd;

    cd = get_co_data(node_type, obj);
    if (cd == nullptr) {
        LOG_ERROR("can not find conn, node_type: %s", node_type.c_str());
        return ERR_CAN_NOT_FIND_CONN;
    }

    /* add task, then wait to handle. */
    task = new task_t{GetCurrThreadCo(), node_type, obj, head_in, body_in, ERR_OK, head_out, body_out};
    cd->tasks.push(task);

    co_cond_signal(cd->cond);
    LOG_TRACE("signal co handler! node: %s, co: %p", node_type.c_str(), cd->co);
    co_yield_ct();
    LOG_TRACE("signal co handler done! node: %s, co: %p", node_type.c_str(), cd->co);

    ret = task->ret;
    SAFE_DELETE(task);
    return ret;
}

NodeConn::co_data_t* NodeConn::get_co_data(const std::string& node_type, const std::string& obj) {
    int hash;
    node_t* node;
    co_data_t* cd;
    std::string node_id;
    node_conn_data_t* conn_data;

    node = m_net->nodes()->get_node_in_hash(node_type, obj);
    if (node == nullptr) {
        LOG_ERROR("can not find node type: %s", node_type.c_str());
        return nullptr;
    }

    node_id = format_nodes_id(node->host, node->port, node->worker_index);

    auto it = m_coroutines.find(node_id);
    if (it == m_coroutines.end()) {
        conn_data = new node_conn_data_t;
        m_coroutines[node_id] = conn_data;
    } else {
        conn_data = (node_conn_data_t*)it->second;
        if (conn_data->coroutines.size() >= conn_data->max_co_cnt) {
            hash = hash_fnv1_64(obj.c_str(), obj.size());
            cd = conn_data->coroutines[hash % conn_data->coroutines.size()];
            return cd;
        }
    }

    /* create co_data_t. */
    cd = new co_data_t;
    cd->node_type = node_type;
    cd->host = node->host;
    cd->port = node->port;
    cd->worker_index = node->worker_index;
    cd->c = nullptr;
    cd->privdata = this;
    cd->cond = co_cond_alloc();

    conn_data->coroutines.push_back(cd);

    /* create coroutines. */
    co_create(&(cd->co), nullptr, co_handle_task, cd);
    co_resume(cd->co);
    return cd;
}

void* NodeConn::co_handle_task(void* arg) {
    co_enable_hook_sys();
    co_data_t* rds_co = (co_data_t*)arg;
    NodeConn* m = (NodeConn*)rds_co->privdata;
    return m->handle_task(arg);
}

void* NodeConn::handle_task(void* arg) {
    int ret;
    task_t* task;
    MsgHead head;
    MsgBody body;
    co_data_t* cd = (co_data_t*)arg;

    for (;;) {
        if (cd->tasks.empty()) {
            // LOG_TRACE("wait for task! node: %s, co: %p", cd->node_type.c_str(), cd->co);
            co_cond_timedwait(cd->cond, HEART_BEAT_TIME);
        }

        if (cd->c == nullptr) {
            cd->c = node_connect(cd->node_type, cd->host, cd->port, cd->worker_index);
            if (cd->c == nullptr) {
                clear_co_tasks(cd);
                continue;
            }
        }

        if (cd->tasks.empty()) {
            ret = m_net->sys_cmd()->send_heart_beat(cd->c);
            if (ret == ERR_OK) {
                ret = recv_data(cd->c, &head, &body);
                if (ret == ERR_OK) {
                    continue;
                }
            }
        } else {
            task = cd->tasks.front();
            cd->tasks.pop();

            ret = m_net->send_to(cd->c, *task->head_in, *task->body_in);
            if (ret == ERR_OK) {
                ret = recv_data(cd->c, task->head_out, task->body_out);
            }

            task->ret = ret;
            co_resume(task->co);
        }

        if (ret != ERR_OK) {
            LOG_ERROR("conn handle failed! ret: %d, fd: %d", ret, cd->c->fd());
            m_net->close_conn(cd->c);
            cd->c = nullptr;
            clear_co_tasks(cd);
            continue;
        }
    }

    return 0;
}

int NodeConn::recv_data(Connection* c, MsgHead* head, MsgBody* body) {
    Codec::STATUS codec_res;

    for (;;) {
        codec_res = c->conn_read(*head, *body);
        if (codec_res == Codec::STATUS::OK) {
            return ERR_OK;
        } else if (codec_res == Codec::STATUS::PAUSE) {
            if (m_net->now() - c->active_time() > MAX_RECV_DATA_TIME) {
                return ERR_READ_DATA_TIMEOUT;
            }
            co_sleep(100, c->fd(), POLLIN);
            continue;
        } else if (codec_res == Codec::STATUS::CLOSED) {
            return ERR_CONN_CLOSED;
        } else {
            return ERR_READ_DATA_FAILED;
        }
    }
}

void NodeConn::clear_co_tasks(co_data_t* cd) {
    task_t* task;

    while (!cd->tasks.empty()) {
        task = cd->tasks.front();
        cd->tasks.pop();
        task->ret = ERR_NODE_CONNECT_FAILED;
        co_resume(task->co);
    }
}

Connection* NodeConn::node_connect(const std::string& node_type, const std::string& host, int port, int worker_index) {
    int ret;
    Connection* c;

    c = auto_connect(host, port, worker_index);
    if (c == nullptr) {
        LOG_ERROR("node connect failed! node: %s, host: %s, port: %d, index: %d",
                  node_type.c_str(), host.c_str(), port, worker_index);
        return nullptr;
    }

    /* 
    * A1 worker connects to B0's worker (A1 --> B1). 
    * https://wenfh2020.com/2020/10/23/kimserver-node-contact/ 
    * */
    if (c->is_try_connect()) {
        if (m_net->sys_cmd()->send_connect_req_to_worker(c) != ERR_OK) {
            LOG_ERROR("send CMD_REQ_CONNECT_TO_WORKER failed! fd: %d", c->fd());
            m_net->close_conn(c);
            return nullptr;
        }

        /* handle system message. */
        for (;;) {
            ret = handle_sys_message(c);
            if (ret != ERR_OK) {
                LOG_ERROR("handle message failed! fd: %d, ret: %d", c->fd(), ret);
                break;
            }

            /* update connection's status in SysCmd::on_rsp_tell_worker. */
            if (c->is_connected()) {
                LOG_INFO("node connect done! node: %s, host: %s, port: %d, index: %d",
                         node_type.c_str(), host.c_str(), port, worker_index);
                break;
            }

            if (m_net->now() - c->active_time() > 2000) {
                LOG_ERROR("node connect timeout! node: %s, host: %s, port: %d, index: %d",
                          node_type.c_str(), host.c_str(), port, worker_index);
                break;
            }

            co_sleep(200, c->fd(), POLLIN);
        }

        if (!c->is_connected()) {
            LOG_ERROR("node connect failed! node: %s, host: %s, port: %d, index: %d",
                      node_type.c_str(), host.c_str(), port, worker_index);
            m_net->close_conn(c);
            c = nullptr;
        }
    }

    return c;
}

Connection* NodeConn::auto_connect(const std::string& host, int port, int worker_index) {
    int fd;
    Connection* c;
    std::string node_id;

    /* auto connect. */
    int rv, ret;
    char portstr[6];
    struct addrinfo hints, *servinfo, *p;
    sockaddr saddr;
    size_t saddrlen;
    bool completed = false;

    snprintf(portstr, sizeof(portstr), "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    LOG_DEBUG("get addr info, host: %s", host.c_str());

    if ((rv = getaddrinfo(host.c_str(), portstr, &hints, &servinfo)) != 0) {
        LOG_ERROR("get addr info failed! host: %s, err: %s",
                  host.c_str(), gai_strerror(rv));
        return nullptr;
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {
        /* Try to create the socket and to connect it.
             * If we fail in the socket() call, or on connect(), we retry with
             * the next entry in servinfo. */
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd != -1) {
            break;
        }
    }

    if (fd == -1) {
        LOG_ERROR("create socket failed! host: %s", host.c_str());
        freeaddrinfo(servinfo);
        return nullptr;
    }

    saddrlen = p->ai_addrlen;
    memcpy(&saddr, p->ai_addr, p->ai_addrlen);
    freeaddrinfo(servinfo);

    c = m_net->create_conn(fd);
    if (c == nullptr) {
        LOG_ERROR("create conn failed! fd: %d", fd);
        m_net->close_fd(fd);
        return nullptr;
    }

    if (anet_no_block(m_errstr, fd) != ANET_OK) {
        LOG_ERROR("set socket no block failed! fd: %d, errstr: %s", fd, m_errstr);
        goto error;
    }

    if (anet_keep_alive(m_errstr, fd, 100) != ANET_OK) {
        LOG_ERROR("set socket keep alive failed! fd: %d, errstr: %s", fd, m_errstr);
        goto error;
    }

    if (anet_set_tcp_no_delay(m_errstr, fd, 1) != ANET_OK) {
        LOG_ERROR("set socket no delay failed! fd: %d, errstr: %s", fd, m_errstr);
        goto error;
    }

    c->init(Codec::TYPE::PROTOBUF);
    c->set_privdata(this);
    c->set_active_time(mstime());
    c->set_system(true);
    c->set_state(Connection::STATE::TRY_CONNECT);

    /* A1 connect to B1, and save B1's connection. */
    node_id = format_nodes_id(host, port, worker_index);
    c->set_node_id(node_id);

    /* check connect. */
    for (int i = 0; i < 3; i++) {
        ret = anet_check_connect_done(fd, &saddr, saddrlen, completed);
        if (ret == ANET_ERR) {
            LOG_ERROR("connect failed! host: %s, port: %d", host.c_str(), port);
            goto error;
        }
        if (completed) {
            LOG_DEBUG("connect node done! %s:%d", host.c_str(), port);
            return c;
        }
        co_sleep(1000, fd, POLLOUT);
    }

    if (!completed) {
        LOG_ERROR("set socket no delay failed! fd: %d, errstr: %s", fd, m_errstr);
        goto error;
    }

    return c;

error:
    m_net->close_conn(fd);
    return nullptr;
}

void NodeConn::co_sleep(int ms, int fd, int events) {
    struct pollfd pf = {0};
    pf.fd = fd;
    pf.events = events | POLLERR | POLLHUP;
    poll(&pf, 1, ms);
}

/* for nodes connect. */
int NodeConn::handle_sys_message(Connection* c) {
    int fd, ret;
    Request* req;
    Codec::STATUS codec_res;

    fd = c->fd();
    ret = ERR_OK;
    req = new Request(c->fd_data());

    for (;;) {
        codec_res = c->conn_read(*req->msg_head(), *req->msg_body());
        if (codec_res != Codec::STATUS::OK) {
            if (codec_res == Codec::STATUS::ERR || codec_res == Codec::STATUS::CLOSED) {
                ret = ERR_READ_DATA_FAILED;
                LOG_ERROR("conn read failed. codec res: %d, fd: %d", codec_res, fd);
            }
            break;
        }

        ret = m_net->sys_cmd()->handle_msg(req);
        if (ret != ERR_OK) {
            LOG_ERROR("handle sys msg failed! fd: %d", req->fd());
            break;
        }

        req->msg_head()->Clear();
        req->msg_body()->Clear();
        ret = ERR_OK;
    }

    SAFE_DELETE(req);
    return ret;
}

}  // namespace kim