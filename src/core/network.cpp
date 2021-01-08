#include "network.h"

namespace kim {

Network::Network(Log* logger, TYPE type) : m_logger(logger), m_type(type) {
}

Network::~Network() {
    destory();
}

void Network::clear_routines() {
    for (const auto& co : m_coroutines) {
        co_release(co);
    }
    m_coroutines.clear();
    FreeLibcoEnv();
}

void Network::destory() {
    // end_ev_loop();
    close_fds();
    clear_routines();

    for (const auto& it : m_wait_send_fds) {
        free(it);
    }
    m_wait_send_fds.clear();
}

void Network::run() {
    LOG_INFO("network run: %d", (int)m_type);
    co_eventloop(co_get_epoll_ct(), 0, 0);
}

void Network::close_fds() {
    for (const auto& it : m_conns) {
        Connection* c = it.second;
        if (c && !c->is_invalid()) {
            close_fd(c->fd());
            SAFE_DELETE(c);
        }
    }
    m_conns.clear();
}

void Network::close_chanel(int* fds) {
    LOG_DEBUG("close chanel, fd0: %d, fd1: %d", fds[0], fds[1]);
    close_fd(fds[0]);
    close_fd(fds[1]);
}

bool Network::close_conn(Connection* c) {
    if (c == nullptr) {
        return false;
    }
    return close_conn(c->fd());
}

bool Network::close_conn(int fd) {
    LOG_TRACE("close conn, fd: %d", fd);

    if (fd == -1) {
        LOG_ERROR("invalid fd: %d", fd);
        return false;
    }

    auto it = m_conns.find(fd);
    if (it == m_conns.end()) {
        return false;
    }

    Connection* c = it->second;
    c->set_state(Connection::STATE::CLOSED);

    if (!c->get_node_id().empty()) {
        m_node_conns.erase(c->get_node_id());
    }

    close_fd(fd);
    SAFE_DELETE(c);
    m_conns.erase(it);
    return true;
}

bool Network::set_gate_codec(const std::string& codec_type) {
    Codec::TYPE type = Codec::get_codec_type(codec_type);
    if (type != Codec::TYPE::UNKNOWN) {
        m_gate_codec = type;
        return true;
    }
    return false;
}

int Network::listen_to_port(const char* host, int port) {
    int fd = -1;
    char errstr[256];

    fd = anet_tcp_server(errstr, host, port, TCP_BACK_LOG);
    if (fd == -1) {
        LOG_ERROR("bind tcp ipv4 failed! %s", errstr);
        return -1;
    }

    if (anet_no_block(m_errstr, fd) != ANET_OK) {
        LOG_ERROR("set socket no block failed! fd: %d, errstr: %s", fd, m_errstr);
        close_fd(fd);
        return -1;
    }

    LOG_INFO("listen to port, %s:%d", host, port);
    return fd;
}

void Network::close_fd(int fd) {
    if (close(fd) == -1) {
        LOG_WARN("close channel failed, fd: %d. errno: %d, errstr: %s",
                 fd, errno, strerror(errno));
    }
    LOG_DEBUG("close fd: %d.", fd);
}

void* Network::co_handler_accept_nodes_conn(void*) {
    co_enable_hook_sys();
    return 0;
}

void* Network::co_handler_accept_gate_conn(void* d) {
    co_enable_hook_sys();
    Connection* c = (Connection*)d;
    Network* net = (Network*)c->privdata();
    return net->handler_accept_gate_conn(d);
}

/* 创建新的子协程，创建新的 connection. */
void* Network::handler_accept_gate_conn(void* d) {
    channel_t ch;
    chanel_resend_data_t* ch_data;
    char ip[NET_IP_STR_LEN] = {0};
    int fd, port, family, chanel_fd, err;

    for (;;) {
        fd = anet_tcp_accept(m_errstr, m_gate_host_fd, ip, sizeof(ip), &port, &family);
        if (fd == ANET_ERR) {
            if (errno != EWOULDBLOCK) {
                LOG_WARN("accepting client connection: %s", m_errstr);
            }
            co_sleep(1000, fd);
            continue;
        }

        LOG_INFO("accepted client: %s:%d, fd: %d", ip, port, fd);

        chanel_fd = m_worker_data_mgr->get_next_worker_data_fd();
        if (chanel_fd <= 0) {
            LOG_ERROR("find next worker chanel failed!");
            close_fd(fd);
            continue;
        }

        LOG_TRACE("send client fd: %d to worker through chanel fd %d", fd, chanel_fd);

        ch = {fd, family, static_cast<int>(m_gate_codec), 0};
        err = write_channel(chanel_fd, &ch, sizeof(channel_t), m_logger);
        if (err != 0) {
            if (err == EAGAIN) {
                /* re send again in timer. */
                ch_data = (chanel_resend_data_t*)malloc(sizeof(chanel_resend_data_t));
                memset(ch_data, 0, sizeof(chanel_resend_data_t));
                ch_data->ch = ch;
                m_wait_send_fds.push_back(ch_data);
                LOG_TRACE("wait to write channel, errno: %d", err);
                continue;
            }
            LOG_ERROR("write channel failed! errno: %d", err);
        }

        close_fd(fd);
        continue;
    }

    LOG_WARN("exit gate accept coroutine!");
    return 0;
}

void* Network::co_handler_requests(void* d) {
    co_enable_hook_sys();
    Connection* c = (Connection*)d;
    Network* net = (Network*)c->privdata();
    return net->handler_requests(d);
}

void* Network::handler_requests(void* d) {
    int fd;
    Connection *c, *conn;

    c = (Connection*)d;
    fd = c->fd();

    for (;;) {
        auto it = m_conns.find(fd);
        if (it == m_conns.end() || it->second == nullptr) {
            LOG_WARN("find connection failed, fd: %d", fd);
            close_conn(fd);
            return 0;
        }

        conn = it->second;
        if (conn != c) {
            LOG_ERROR("ensure connection, fd: %d", fd);
            close_conn(fd);
            return 0;
        }

        if (conn->is_invalid()) {
            LOG_ERROR("invalid socket, fd: %d", fd);
            close_conn(fd);
            return 0;
        }

        // return process_msg(conn);
    }

    return 0;
}

bool Network::process_msg(Connection* c) {
    return (c->is_http()) ? process_http_msg(c) : process_tcp_msg(c);
}

bool Network::process_tcp_msg(Connection* c) {
    int fd, res;
    MsgHead head;
    MsgBody body;
    Codec::STATUS codec_res;
    uint32_t old_cnt, old_bytes;

    fd = c->fd();
    res = ERR_UNKOWN_CMD;
    old_cnt = c->read_cnt();
    old_bytes = c->read_bytes();

    codec_res = c->conn_read(head, body);
    if (codec_res != Codec::STATUS::ERR) {
        m_payload.set_read_cnt(m_payload.read_cnt() + (c->read_cnt() - old_cnt));
        m_payload.set_read_bytes(m_payload.read_bytes() + (c->read_bytes() - old_bytes));
    }

    LOG_TRACE("conn read result, fd: %d, ret: %d", fd, (int)codec_res);

    while (codec_res == Codec::STATUS::OK) {
        if (res == ERR_UNKOWN_CMD) {
            res = m_module_mgr->handle_request(c->fd_data(), head, body);
            if (res == ERR_UNKOWN_CMD) {
                LOG_WARN("find cmd handler failed! fd: %d, cmd: %d", fd, head.cmd());
            } else {
                if (res != ERR_OK) {
                    LOG_TRACE("process tcp msg failed! fd: %d", fd);
                }
            }
        }

        head.Clear();
        body.Clear();

        codec_res = c->fetch_data(head, body);
        if (codec_res == Codec::STATUS::ERR || codec_res == Codec::STATUS::CLOSED) {
            LOG_TRACE("conn read failed. fd: %d", fd);
            goto error;
        }
    }

    if (codec_res == Codec::STATUS::ERR || codec_res == Codec::STATUS::CLOSED) {
        LOG_TRACE("conn read failed. fd: %d", fd);
        goto error;
    }

    return true;

error:
    close_conn(c);
    return false;
}

bool Network::process_http_msg(Connection* c) {
    // HttpMsg msg;
    // int old_cnt, old_bytes;
    // Cmd::STATUS cmd_ret;
    // Codec::STATUS codec_res;
    // Request req(c->fd_data(), true);

    // old_cnt = c->read_cnt();
    // old_bytes = c->read_bytes();

    // codec_res = c->conn_read(*req.http_msg());
    // if (codec_res != Codec::STATUS::ERR) {
    //     m_payload.set_read_cnt(m_payload.read_cnt() + (c->read_cnt() - old_cnt));
    //     m_payload.set_read_bytes(m_payload.read_bytes() + (c->read_bytes() - old_bytes));
    // }

    // LOG_TRACE("connection is http, read ret: %d", (int)codec_res);

    // while (codec_res == Codec::STATUS::OK) {
    //     cmd_ret = m_module_mgr->process_req(req);
    //     msg.Clear();
    //     codec_res = c->fetch_data(*req.http_msg());
    //     LOG_TRACE("cmd status: %d", cmd_ret);
    // }

    // if (codec_res == Codec::STATUS::ERR || codec_res == Codec::STATUS::CLOSED) {
    //     LOG_TRACE("conn read failed. fd: %d", c->fd());
    //     close_conn(c);
    //     return false;
    // }

    return true;
}

Connection* Network::create_conn(int fd) {
    auto it = m_conns.find(fd);
    if (it != m_conns.end()) {
        LOG_WARN("find old connection, fd: %d", fd);
        close_conn(fd);
    }

    uint64_t seq;
    Connection* c;

    seq = new_seq();
    c = new Connection(m_logger, fd, seq);
    if (c == nullptr) {
        LOG_ERROR("new connection failed! fd: %d", fd);
        return nullptr;
    }

    m_conns[fd] = c;
    c->set_keep_alive(m_keep_alive);
    LOG_DEBUG("create connection fd: %d, seq: %llu", fd, seq);
    return c;
}

Connection* Network::create_conn(int fd, Codec::TYPE codec, bool is_chanel) {
    if (anet_no_block(m_errstr, fd) != ANET_OK) {
        LOG_ERROR("set socket no block failed! fd: %d, errstr: %s", fd, m_errstr);
        return nullptr;
    }

    if (!is_chanel) {
        if (anet_keep_alive(m_errstr, fd, 100) != ANET_OK) {
            LOG_ERROR("set socket keep alive failed! fd: %d, errstr: %s", fd, m_errstr);
            return nullptr;
        }
        if (anet_set_tcp_no_delay(m_errstr, fd, 1) != ANET_OK) {
            LOG_ERROR("set socket no delay failed! fd: %d, errstr: %s", fd, m_errstr);
            return nullptr;
        }
    }

    Connection* c = create_conn(fd);
    if (c == nullptr) {
        close_fd(fd);
        LOG_ERROR("add chanel event failed! fd: %d", fd);
        return nullptr;
    }
    c->init(codec);
    c->set_privdata(this);
    c->set_active_time(now());
    c->set_state(Connection::STATE::CONNECTED);

    if (is_chanel) {
        c->set_system(true);
    }

    LOG_TRACE("create connection done! fd: %d", fd);
    return c;
}

bool Network::load_public(const CJsonObject& config) {
    if (!load_config(config)) {
        LOG_ERROR("load config failed!");
        return false;
    }

    m_nodes = new Nodes(m_logger);
    if (m_nodes == nullptr) {
        LOG_ERROR("alloc nodes failed!");
        return false;
    }

    LOG_INFO("load public done!");
    return true;
}

bool Network::load_modules() {
    m_module_mgr = new ModuleMgr(m_logger);
    if (m_module_mgr == nullptr || !m_module_mgr->init(m_conf)) {
        return false;
    }
    LOG_INFO("load modules mgr sucess!");
    return true;
}

bool Network::load_config(const CJsonObject& config) {
    double secs;
    m_conf = config;
    std::string codec;

    codec = m_conf("gate_codec");
    if (!codec.empty()) {
        if (!set_gate_codec(codec)) {
            LOG_ERROR("invalid codec: %s", codec.c_str());
            return false;
        }
        LOG_DEBUG("gate codec: %s", codec.c_str());
    }

    if (m_conf.Get("keep_alive", secs)) {
        set_keep_alive(secs);
    }

    m_node_type = m_conf("node_type");
    if (m_node_type.empty()) {
        LOG_ERROR("invalid inner node info!");
        return false;
    }

    return true;
}

bool Network::load_worker_data_mgr() {
    m_worker_data_mgr = new WorkerDataMgr(m_logger);
    return (m_worker_data_mgr != nullptr);
}

/* parent. */
bool Network::create_m(const addr_info* ai, const CJsonObject& config) {
    if (ai == nullptr) {
        return false;
    }

    int fd = -1;
    Connection* c;

    if (!load_public(config)) {
        LOG_ERROR("load public failed!");
        return false;
    }

    if (!load_worker_data_mgr()) {
        LOG_ERROR("new worker data mgr failed!");
        return false;
    }

    /* inner listen. */
    if (!ai->node_host().empty()) {
        fd = listen_to_port(ai->node_host().c_str(), ai->node_port());
        if (fd == -1) {
            LOG_ERROR("listen to port failed! %s:%d",
                      ai->node_host().c_str(), ai->node_port());
            return false;
        }

        m_node_host_fd = fd;
        m_node_host = ai->node_host();
        m_node_port = ai->node_port();
        LOG_INFO("node fd: %d", m_node_host_fd);

        c = create_conn(m_node_host_fd, Codec::TYPE::PROTOBUF);
        if (c == nullptr) {
            close_fd(m_node_host_fd);
            LOG_ERROR("add read event failed, fd: %d", m_node_host_fd);
            return false;
        }

        /* 启动集群内部 accept 协程。 */
    }

    /* gate listen. */
    if (!ai->gate_host().empty()) {
        fd = listen_to_port(ai->gate_host().c_str(), ai->gate_port());
        if (fd == -1) {
            LOG_ERROR("listen to gate failed! %s:%d",
                      ai->gate_host().c_str(), ai->gate_port());
            return false;
        }

        m_gate_host_fd = fd;
        m_gate_host = ai->gate_host();
        m_gate_port = ai->gate_port();
        LOG_INFO("gate fd: %d, host: %s, port: %d",
                 m_gate_host_fd, m_gate_host.c_str(), m_gate_port);

        c = create_conn(m_gate_host_fd, m_gate_codec);
        if (c == nullptr) {
            close_fd(m_gate_host_fd);
            LOG_ERROR("add read event failed, fd: %d", m_gate_host_fd);
            return false;
        }

        /* co_accecpt_node_conns */
        stCoRoutine_t* co;
        co_create(&co, NULL, co_handler_accept_gate_conn, (void*)c);
        co_resume(co);
        m_coroutines.insert(co);
    }

    return true;
}

void Network::co_sleep(int ms, int fd) {
    struct pollfd pf = {0};
    pf.fd = fd;
    poll(&pf, 1, ms);
}

void* Network::handler_read_transfer_fd(void* d) {
    int err;
    int data_fd;
    channel_t ch;
    Codec::TYPE codec;
    Connection *c = nullptr, *conn_data = nullptr;

    conn_data = (Connection*)d;
    data_fd = conn_data->fd();

    for (;;) {
        // read fd from manager.
        err = read_channel(data_fd, &ch, sizeof(channel_t), m_logger);
        if (err != 0) {
            if (err == EAGAIN) {
                // LOG_TRACE("read channel again next time! channel fd: %d", data_fd);
                co_sleep(1000, data_fd);
                continue;
            } else {
                destory();
                LOG_CRIT("read channel failed, exit! channel fd: %d", data_fd);
                exit(EXIT_FD_TRANSFER);
            }
        }

        codec = static_cast<Codec::TYPE>(ch.codec);
        c = create_conn(ch.fd, codec);
        if (c == nullptr) {
            LOG_ERROR("add data fd read event failed, fd: %d", ch.fd);
            close_conn(ch.fd);
            continue;
        }

        if (ch.is_system) {
            c->set_system(true);
        }

        LOG_TRACE("read from channel, get data: fd: %d, family: %d, codec: %d, system: %d",
                  ch.fd, ch.family, ch.codec, ch.is_system);
        // co_sleep(data_fd, 1000);
        /* catch new fd, and create new routines. */
        stCoRoutine_t* co;
        co_create(&co, NULL, co_handler_requests, (void*)c);
        co_resume(co);
        m_coroutines.insert(co);
    }

    return 0;
}

void* Network::co_handler_read_transfer_fd(void* d) {
    co_enable_hook_sys();
    Connection* c = (Connection*)d;
    Network* net = (Network*)c->privdata();
    return net->handler_read_transfer_fd(d);
}

/* worker. */
bool Network::create_w(const CJsonObject& config, int ctrl_fd, int data_fd, int index) {
    if (!load_public(config)) {
        LOG_ERROR("load public failed!");
        return false;
    }

    if (!load_modules()) {
        LOG_ERROR("load module failed!");
        return false;
    }

    Connection *conn_ctrl, *conn_data;

    conn_ctrl = create_conn(ctrl_fd, Codec::TYPE::PROTOBUF, true);
    if (conn_ctrl == nullptr) {
        close_fd(ctrl_fd);
        LOG_ERROR("add read event failed, fd: %d", ctrl_fd);
        return false;
    }

    conn_data = create_conn(data_fd, Codec::TYPE::PROTOBUF, true);
    if (conn_data == nullptr) {
        close_fd(data_fd);
        LOG_ERROR("add read event failed, fd: %d", data_fd);
        return false;
    }

    m_conf = config;
    m_manager_ctrl_fd = ctrl_fd;
    m_manager_data_fd = data_fd;
    m_worker_index = index;
    LOG_INFO("create network done!");

    stCoRoutine_t* co;
    co_create(&co, NULL, co_handler_read_transfer_fd, (void*)conn_data);
    co_resume(co);
    m_coroutines.insert(co);
    return true;
}

}  // namespace kim