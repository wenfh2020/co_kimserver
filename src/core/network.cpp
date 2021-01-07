#include "network.h"

#include "connection.h"
#include "server.h"

namespace kim {

Network::Network(Log* logger, TYPE type) : m_logger(logger), m_type(type) {
}

Network::~Network() {
    destory();
}

void Network::close_conns() {
    LOG_TRACE("close_conns(), cnt: %d", m_conns.size());
    for (const auto& it : m_conns) {
        close_conn(it.second);
    }
}

void Network::destory() {
    // end_ev_loop();
    close_conns();

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
        if (!c->is_invalid()) {
            close_conn(c);
        }
    }
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

    for (;;) {
    }

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
            struct pollfd pf = {0};
            pf.fd = -1;
            poll(&pf, 1, 1000);
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

    return 0;
}

void* Network::co_handler_requests(void*) {
    co_enable_hook_sys();

    for (;;) {
    }

    return 0;
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
        stCoRoutine_t* co_accecpt_gate_conns;
        co_create(&co_accecpt_gate_conns, NULL, co_handler_accept_gate_conn, (void*)c);
        co_resume(co_accecpt_gate_conns);
    }

    return true;
}

void Network::co_sleep(int fd, int ms) {
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
                LOG_TRACE("read channel again next time! channel fd: %d", data_fd);
                co_sleep(data_fd, 1000);
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
        co_sleep(data_fd, 1000);
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
    return true;
}

}  // namespace kim