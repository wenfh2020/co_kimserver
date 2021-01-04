#include "network.h"

#include "connection.h"
#include "server.h"

namespace kim {

Network::Network(Log* logger, TYPE type) : m_type(type) {
}

Network::~Network() {
    destory();
}

void Network::destory() {
}

void Network::run() {
    co_eventloop(co_get_epoll_ct(), 0, 0);
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
            return 0;
        }
    }

    return 0;
}

void* Network::co_handler_accept_gate_conn(void* d) {
    co_enable_hook_sys();

    /* 创建新的子协程，创建新的 connection. */

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
        // close_conn(fd);
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

// Connection* Network::create_conn(int fd, Codec::TYPE codec, bool is_chanel) {
//     if (anet_no_block(m_errstr, fd) != ANET_OK) {
//         LOG_ERROR("set socket no block failed! fd: %d, errstr: %s", fd, m_errstr);
//         return nullptr;
//     }

//     return nullptr;
// }

/* parent. */
bool Network::create_m(const addr_info* ai, const CJsonObject& config) {
    if (ai == nullptr) {
        return false;
    }

    int fd = -1;

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

        // if (!add_read_event(m_node_host_fd, Codec::TYPE::PROTOBUF)) {
        //     close_fd(m_node_host_fd);
        //     LOG_ERROR("add read event failed, fd: %d", m_node_host_fd);
        //     return false;
        // }

        /* 创建 accept 协程。 
         * 在 accpet 处理函数里创建新的协程，但是需要限制它们的大小。*/

        // stCoRoutine_t *co_accecpt_node_conns, co_accecpt_gate_conns;
        // co_create(&co_accecpt_node_conns, NULL, co_handler_accept_nodes_conn, 0);
        // co_resume(co_accecpt_node_conns);
    }

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
        LOG_INFO("gate fd: %d", m_gate_host_fd);

        // if (!add_read_event(m_gate_host_fd, m_gate_codec)) {
        //     close_fd(m_gate_host_fd);
        //     LOG_ERROR("add read event failed, fd: %d", m_gate_host_fd);
        //     return false;
        // }

        stCoRoutine_t* co_accecpt_gate_conns;
        co_create(&co_accecpt_gate_conns, NULL, co_handler_accept_gate_conn, 0);
        co_resume(co_accecpt_gate_conns);
    }

    return true;
}

/* worker. */
bool Network::create_w(const CJsonObject& config, int ctrl_fd, int data_fd, int index) {
    return true;
}

}  // namespace kim