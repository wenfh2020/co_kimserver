#include "network.h"

#include "request.h"

#define MAX_TRY_RESEND_FD_CNT 10

namespace kim {

Network::Network(Log* logger, TYPE type) : m_logger(logger), m_type(type) {
}

Network::~Network() {
    destory();
}

void Network::clear_routines() {
    SAFE_DELETE(m_coroutines);
    FreeLibcoEnv();
}

void Network::destory() {
    close_fds();
    clear_routines();
    for (const auto& it : m_wait_send_fds) {
        free(it);
    }
    m_wait_send_fds.clear();

    SAFE_DELETE(m_zk_mgr);
    SAFE_DELETE(m_module_mgr);
    SAFE_DELETE(m_mysql_mgr);
}

void Network::run() {
    LOG_INFO("network run: %d", (int)m_type);
    if (m_coroutines != nullptr) {
        m_coroutines->run();
    }
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

    fd = anet_tcp_server(m_errstr, host, port, TCP_BACK_LOG);
    if (fd == -1) {
        LOG_ERROR("bind tcp ipv4 failed! %s", m_errstr);
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

void* Network::co_handle_accept_nodes_conn(void*) {
    co_enable_hook_sys();
    return 0;
}

void Network::co_handle_timer() {
    if (is_manager()) {
        check_wait_send_fds();
        if (m_zk_mgr != nullptr) {
            m_zk_mgr->on_repeat_timer();
        }
    }
}

void Network::check_wait_send_fds() {
    int err;
    int chanel_fd;
    chanel_resend_data_t* data;

    for (auto it = m_wait_send_fds.begin(); it != m_wait_send_fds.end();) {
        data = *it;

        chanel_fd = m_worker_data_mgr->get_next_worker_data_fd();
        if (chanel_fd <= 0) {
            LOG_ERROR("can not find next worker chanel!");
            return;
        }

        err = write_channel(chanel_fd, &data->ch, sizeof(channel_t), m_logger);
        if (err == 0 || (err != 0 && err != EAGAIN) ||
            ((err == EAGAIN) && (++data->count >= MAX_TRY_RESEND_FD_CNT))) {
            if (err != 0) {
                LOG_ERROR("resend chanel failed! fd: %d, errno: %d", data->ch.fd, err);
            }
            close_fd(data->ch.fd);
            free(data);
            m_wait_send_fds.erase(it++);
            continue;
        }

        it++;
        LOG_DEBUG("wait to write channel, errno: %d", err);
    }
}

void* Network::co_handle_accept_gate_conn(void* d) {
    co_enable_hook_sys();
    co_task_t* task = (co_task_t*)d;
    Connection* c = task->c;
    Network* net = (Network*)c->privdata();
    return net->handle_accept_gate_conn(d);
}

void* Network::handle_accept_gate_conn(void* d) {
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
            m_coroutines->co_sleep(1000, m_gate_host_fd, POLLIN);
            continue;
        }

        LOG_INFO("accepted client: %s:%d, fd: %d", ip, port, fd);

        chanel_fd = m_worker_data_mgr->get_next_worker_data_fd();
        if (chanel_fd <= 0) {
            LOG_ERROR("find next worker chanel failed!");
            close_fd(fd);
            continue;
        }

        LOG_DEBUG("send client fd: %d to worker through chanel fd %d", fd, chanel_fd);

        /* parent process transfers fd to child. */
        ch = {fd, family, static_cast<int>(m_gate_codec), 0};
        err = write_channel(chanel_fd, &ch, sizeof(channel_t), m_logger);
        if (err != 0) {
            if (err == EAGAIN) {
                /* re send again in timer. */
                ch_data = (chanel_resend_data_t*)calloc(1, sizeof(chanel_resend_data_t));
                ch_data->ch = ch;
                m_wait_send_fds.push_back(ch_data);
                LOG_DEBUG("wait to write channel, errno: %d", err);
                m_coroutines->co_sleep(100, m_gate_host_fd);
                continue;
            }
            LOG_ERROR("write channel failed! errno: %d", err);
        }
        close_fd(fd);
        continue;
    }

    return 0;
}

void* Network::co_handle_requests(void* d) {
    co_enable_hook_sys();
    co_task_t* task = (co_task_t*)d;
    Network* net = (Network*)task->c->privdata();
    return net->handle_requests(d);
}

void* Network::handle_requests(void* d) {
    int fd;
    Connection* c;
    co_task_t* task;
    task = (co_task_t*)d;

    for (;;) {
        if (task->c == nullptr) {
            m_coroutines->add_free_co_task(task);
            co_yield_ct();
            continue;
        }

        c = (Connection*)task->c;
        fd = c->fd();

        auto it = m_conns.find(fd);
        if (it == m_conns.end() || it->second == nullptr) {
            LOG_WARN("find connection failed, fd: %d", fd);
            close_conn(fd);
            task->c = nullptr;
            continue;
        }

        if (c->is_invalid()) {
            LOG_ERROR("invalid socket, fd: %d", fd);
            close_conn(fd);
            task->c = nullptr;
            continue;
        }

        for (;;) {
            /* check connection alive. */
            if (now() - c->active_time() > m_keep_alive) {
                close_conn(fd);
                task->c = nullptr;
                LOG_TRACE("conn timeout, fd: %d, now: %llu, active: %llu, keep alive: %llu\n",
                          fd, now(), c->active_time(), m_keep_alive);
                break;
            }

            if (!process_msg(c)) {
                close_conn(fd);
                task->c = nullptr;
                break;
            }
            m_coroutines->co_sleep(100, fd, POLLIN);
        }
    }

    return 0;
}

bool Network::process_msg(Connection* c) {
    return (c->is_http()) ? process_http_msg(c) : process_tcp_msg(c);
}

bool Network::process_tcp_msg(Connection* c) {
    LOG_TRACE("handle tcp msg, fd: %d", c->fd());

    int fd, res;
    Request* req;
    MsgHead* head;
    MsgBody* body;
    Codec::STATUS codec_res;
    uint32_t old_cnt, old_bytes;

    fd = c->fd();
    res = ERR_UNKOWN_CMD;
    old_cnt = c->read_cnt();
    old_bytes = c->read_bytes();

    req = new Request(c->fd_data(), false);
    head = req->msg_head();
    body = req->msg_body();

    codec_res = c->conn_read(*head, *body);
    if (codec_res != Codec::STATUS::ERR) {
        m_payload.set_read_cnt(m_payload.read_cnt() + (c->read_cnt() - old_cnt));
        m_payload.set_read_bytes(m_payload.read_bytes() + (c->read_bytes() - old_bytes));
    }

    LOG_TRACE("conn read result, fd: %d, ret: %d", fd, (int)codec_res);

    while (codec_res == Codec::STATUS::OK) {
        if (res == ERR_UNKOWN_CMD) {
            res = m_module_mgr->handle_request(req);
            if (res == ERR_UNKOWN_CMD) {
                LOG_WARN("find cmd handler failed! fd: %d, cmd: %d",
                         fd, head->cmd());
            } else {
                if (res != ERR_OK) {
                    LOG_TRACE("process tcp msg failed! fd: %d", fd);
                }
            }
        }

        head->Clear();
        body->Clear();
        res = ERR_UNKOWN_CMD;

        /* continue to decode recv buffer. */
        codec_res = c->fetch_data(*head, *body);
        LOG_DEBUG("conn read result, fd: %d, ret: %d", fd, (int)codec_res);
    }

    if (codec_res == Codec::STATUS::ERR || codec_res == Codec::STATUS::CLOSED) {
        LOG_DEBUG("conn read failed. fd: %d", fd);
        SAFE_DELETE(req);
        return false;
    }

    codec_res = c->conn_write();
    if (codec_res == Codec::STATUS::ERR) {
        LOG_DEBUG("conn read failed. fd: %d", fd);
        SAFE_DELETE(req);
        return false;
    } else if (codec_res == Codec::STATUS::PAUSE) {
        m_coroutines->co_sleep(100, fd, POLLOUT);
    }

    SAFE_DELETE(req);
    return true;
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

    if (!load_corotines()) {
        LOG_ERROR("load coroutines failed!");
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
    m_module_mgr = new ModuleMgr(m_logger, this);
    if (m_module_mgr == nullptr || !m_module_mgr->init(m_config)) {
        return false;
    }
    LOG_INFO("load modules mgr sucess!");
    return true;
}

bool Network::load_corotines() {
    m_coroutines = new Coroutines(m_logger);
    return (m_coroutines != nullptr);
}

bool Network::load_config(const CJsonObject& config) {
    int secs;
    m_config = config;
    std::string codec;

    codec = m_config("gate_codec");
    if (!codec.empty()) {
        if (!set_gate_codec(codec)) {
            LOG_ERROR("invalid codec: %s", codec.c_str());
            return false;
        }
        LOG_DEBUG("gate codec: %s", codec.c_str());
    }

    if (m_config.Get("keep_alive", secs)) {
        set_keep_alive(secs);
    }

    m_node_type = m_config("node_type");
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

bool Network::load_zk_mgr() {
    m_zk_mgr = new ZkClient(m_logger, this);
    if (m_zk_mgr == nullptr) {
        LOG_ERROR("new zk mgr failed!");
        return false;
    }

    if (m_zk_mgr->init(m_config)) {
        LOG_INFO("load zk client done!");
    }
    return true;
}

/* parent. */
bool Network::create_m(const addr_info* ai, const CJsonObject& config) {
    if (ai == nullptr) {
        return false;
    }

    int fd = -1;
    Connection* c;
    co_task_t* task;

    if (!load_public(config)) {
        LOG_ERROR("load public failed!");
        return false;
    }

    if (!load_worker_data_mgr()) {
        LOG_ERROR("new worker data mgr failed!");
        return false;
    }

    if (!load_zk_mgr()) {
        LOG_ERROR("load zookeeper mgr failed!");
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

        task = m_coroutines->create_co_task(c, co_handle_accept_gate_conn);
        if (task == nullptr) {
            LOG_ERROR("create new corotines failed!");
            close_conn(c);
            return false;
        }
        co_resume(task->co);
    }

    return true;
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

    co_task_t* task;
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

    m_config = config;
    m_manager_ctrl_fd = ctrl_fd;
    m_manager_data_fd = data_fd;
    m_worker_index = index;
    LOG_INFO("create network done!");

    task = m_coroutines->create_co_task(conn_data, co_handle_read_transfer_fd);
    if (task == nullptr) {
        LOG_ERROR("create new corotines failed!");
        close_conn(conn_data);
        return false;
    }
    co_resume(task->co);
    return true;
}

void* Network::co_handle_read_transfer_fd(void* d) {
    co_enable_hook_sys();
    co_task_t* task = (co_task_t*)d;
    Network* net = (Network*)task->c->privdata();
    return net->handle_read_transfer_fd(d);
}

void* Network::handle_read_transfer_fd(void* d) {
    LOG_TRACE("handle_read_transfer_fd....");

    int err;
    int data_fd;
    channel_t ch;
    co_task_t* task;
    Codec::TYPE codec;
    Connection* c = nullptr;

    task = (co_task_t*)d;
    data_fd = task->c->fd();

    for (;;) {
        /* read fd from parent. */
        err = read_channel(data_fd, &ch, sizeof(channel_t), m_logger);
        if (err != 0) {
            if (err == EAGAIN) {
                LOG_TRACE("read channel again next time! channel fd: %d", data_fd);
                m_coroutines->co_sleep(100, data_fd, POLLIN);
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

        LOG_INFO("read from channel, get data: fd: %d, family: %d, codec: %d, system: %d",
                 ch.fd, ch.family, ch.codec, ch.is_system);

        co_task_t* co_task = m_coroutines->create_co_task(c, co_handle_requests);
        if (co_task == nullptr) {
            LOG_ERROR("create new corotines failed!");
            close_conn(c);
            continue;
        }
        co_resume(co_task->co);
    }

    return 0;
}

int Network::send_to(Connection* c, const MsgHead& head, const MsgBody& body) {
    if (c == nullptr) {
        return false;
    }

    Codec::STATUS ret;
    int old_cnt, old_bytes;

    old_cnt = c->write_cnt();
    old_bytes = c->write_bytes();
    ret = c->conn_write(head, body);
    if (ret != Codec::STATUS::ERR) {
        m_payload.set_write_cnt(m_payload.write_cnt() + (c->write_cnt() - old_cnt));
        m_payload.set_write_bytes(m_payload.write_bytes() + (c->write_bytes() - old_bytes));
    }

    return true;
}

Connection* Network::get_conn(const fd_t& f) {
    auto it = m_conns.find(f.fd);
    return (it == m_conns.end() || it->second->id() != f.id) ? nullptr : it->second;
}

int Network::send_to(const fd_t& f, const MsgHead& head, const MsgBody& body) {
    return send_to(get_conn(f), head, body);
}

int Network::send_ack(const Request* req, int err, const std::string& errstr, const std::string& data) {
    MsgHead head;
    MsgBody body;

    body.set_data(data);
    body.mutable_rsp_result()->set_code(err);
    body.mutable_rsp_result()->set_msg(errstr);

    head.set_seq(req->msg_head()->seq());
    head.set_cmd(req->msg_head()->cmd() + 1);
    head.set_len(body.ByteSizeLong());

    return send_to(req->fd_data(), head, body);
}

}  // namespace kim