#include "network.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include "redis/redis_mgr.h"
#include "request.h"

namespace kim {

Network::Network(Log* logger, TYPE type) : m_logger(logger), m_type(type) {
    m_now_time = mstime();
}

Network::~Network() {
    destory();
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
            LOG_ERROR("listen to port failed! %s:%d", ai->node_host().c_str(), ai->node_port());
            return false;
        }

        m_node_fd = fd;
        m_node_host = ai->node_host();
        m_node_port = ai->node_port();
        LOG_INFO("node fd: %d", m_node_fd);

        c = create_conn(m_node_fd, Codec::TYPE::PROTOBUF);
        if (c == nullptr) {
            close_fd(m_node_fd);
            LOG_ERROR("add read event failed, fd: %d", m_node_fd);
            return false;
        }

        task = m_coroutines->create_co_task(c, co_handle_accept_nodes_conn);
        if (task == nullptr) {
            LOG_ERROR("create new corotines failed!");
            close_conn(c);
            return false;
        }
        co_resume(task->co);
    }

    /* gate listen. */
    if (!ai->gate_host().empty()) {
        fd = listen_to_port(ai->gate_host().c_str(), ai->gate_port());
        if (fd == -1) {
            LOG_ERROR("listen to gate failed! %s:%d",
                      ai->gate_host().c_str(), ai->gate_port());
            return false;
        }

        m_gate_fd = fd;
        m_gate_host = ai->gate_host();
        m_gate_port = ai->gate_port();
        LOG_INFO("gate fd: %d, host: %s, port: %d",
                 m_gate_fd, m_gate_host.c_str(), m_gate_port);

        c = create_conn(m_gate_fd, m_gate_codec);
        if (c == nullptr) {
            close_fd(m_gate_fd);
            LOG_ERROR("add read event failed, fd: %d", m_gate_fd);
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

bool Network::init_manager_channel(fd_t& fctrl, fd_t& fdata) {
    if (!is_manager()) {
        return false;
    }

    co_task_t* task;
    Connection *conn_ctrl, *conn_data;

    conn_ctrl = create_conn(fctrl.fd, Codec::TYPE::PROTOBUF, true);
    if (conn_ctrl == nullptr) {
        close_fd(fctrl.fd);
        LOG_ERROR("add read event failed, fd: %d", fctrl.fd);
        return false;
    }

    conn_data = create_conn(fdata.fd, Codec::TYPE::PROTOBUF, true);
    if (conn_data == nullptr) {
        close_fd(fdata.fd);
        close_conn(conn_ctrl);
        LOG_ERROR("add read event failed, fd: %d", fdata.fd);
        return false;
    }

    fctrl = m_manager_fctrl = conn_ctrl->ft();
    fdata = m_manager_fdata = conn_data->ft();

    /* recv data from worker. */
    task = m_coroutines->create_co_task(conn_ctrl, co_handle_requests);
    if (task == nullptr) {
        LOG_ERROR("create new corotines failed!");
        close_conn(conn_ctrl);
        close_conn(conn_data);
        return false;
    }

    co_resume(task->co);
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

    if (!load_nodes_conn()) {
        LOG_ERROR("load nodes conn failed!");
        return false;
    }

    if (!load_mysql_mgr()) {
        LOG_ERROR("load mysql pool failed!");
        return false;
    }

    if (!load_redis_mgr()) {
        LOG_ERROR("load redis pool failed!");
        return false;
    }

    if (!ensure_files_limit()) {
        LOG_ERROR("ensure files limit failed! limit: %d", m_max_clients);
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
    m_worker_index = index;
    m_manager_fctrl = conn_ctrl->ft();
    m_manager_fdata = conn_data->ft();

    task = m_coroutines->create_co_task(conn_data, co_handle_read_transfer_fd);
    if (task == nullptr) {
        LOG_ERROR("create new corotines failed!");
        close_conn(conn_data);
        return false;
    }
    co_resume(task->co);

    task = m_coroutines->create_co_task(conn_ctrl, co_handle_requests);
    if (task == nullptr) {
        LOG_ERROR("create new corotines failed!");
        close_conn(conn_ctrl);
        return false;
    }
    co_resume(task->co);

    LOG_INFO("create network done!");
    return true;
}

bool Network::ensure_files_limit() {
    int error = 0;
    int file_limit = 0;
    int max_clients = 0;

    max_clients = str_to_int(m_config("max_clients"));
    if (max_clients == 0) {
        return false;
    }

    file_limit = adjust_files_limit(max_clients + CONFIG_MIN_RESERVED_FDS, error);
    file_limit -= CONFIG_MIN_RESERVED_FDS;

    if (file_limit < 0) {
        LOG_ERROR("set files limit: %d failed!", max_clients);
        return false;
    } else if (file_limit < max_clients) {
        LOG_WARN("set files not hit, cur limit: %d, ensure hit: %d, error: %d.",
                 file_limit, max_clients, error);
    } else {
        LOG_INFO("set max clients: %d done!", file_limit);
    }

    m_max_clients = file_limit;
    return true;
}

void Network::run() {
    LOG_INFO("network run: %d", (int)m_type);
    if (m_coroutines != nullptr) {
        m_coroutines->run();
    }
}

void Network::exit_libco() {
    if (m_coroutines != nullptr) {
        m_coroutines->exit_libco();
    }
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

void Network::on_repeat_timer() {
    m_now_time = mstime();

    if (is_manager()) {
        if (m_zk_cli != nullptr) {
            m_zk_cli->on_timer();
        }
        run_with_period(1000) {
            // report_payload_to_zookeeper();
        }
    } else {
        run_with_period(1000) {
            /* send payload info to manager. */
            // report_payload_to_manager();
        }
    }

    if (m_mysql_mgr != nullptr) {
        m_mysql_mgr->on_timer();
    }

    if (m_coroutines != nullptr) {
        m_coroutines->on_timer();
    }
}

bool Network::report_payload_to_zookeeper() {
    if (!is_manager()) {
        LOG_ERROR("report payload to zk, only for manager!");
        return false;
    }

    NodeData* node;
    std::string json_data;
    PayloadStats pls;
    Payload *manager_pl, *worker_pl;
    int cmd_cnt = 0, conn_cnt = 0, read_cnt = 0, write_cnt = 0, read_bytes = 0, write_bytes = 0;
    const std::unordered_map<int, worker_info_t*>& workers = m_worker_data_mgr->get_infos();

    /* node info. */
    node = pls.mutable_node();
    node->set_zk_path(m_nodes->get_my_zk_node_path());
    node->set_node_type(node_type());
    node->set_node_host(node_host());
    node->set_node_port(node_port());
    node->set_gate_host(m_gate_host);
    node->set_gate_port(m_gate_port);
    node->set_worker_cnt(workers.size());

    /* worker payload infos. */
    for (const auto& it : workers) {
        const worker_info_t* info = it.second;
        cmd_cnt += info->payload.cmd_cnt();
        conn_cnt += info->payload.conn_cnt();
        read_cnt += info->payload.read_cnt();
        read_bytes += info->payload.read_bytes();
        write_cnt += info->payload.write_cnt();
        write_bytes += info->payload.write_bytes();
        worker_pl = pls.add_workers();
        *worker_pl = info->payload;
        if (info->payload.worker_index() == 0) {
            worker_pl->set_worker_index(info->index);
        }
    }

    /* manager statistics data results。 */
    manager_pl = pls.mutable_manager();
    manager_pl->set_worker_index(worker_index());
    manager_pl->set_cmd_cnt(cmd_cnt);
    manager_pl->set_conn_cnt(conn_cnt + m_conns.size());
    manager_pl->set_read_cnt(read_cnt + m_payload.read_cnt());
    manager_pl->set_read_bytes(read_bytes + m_payload.read_bytes());
    manager_pl->set_write_cnt(write_cnt + m_payload.write_cnt());
    manager_pl->set_write_bytes(write_bytes + m_payload.write_bytes());
    manager_pl->set_create_time(now());

    m_payload.Clear();

    if (!proto_to_json(pls, json_data)) {
        LOG_ERROR("proto to json failed!");
        return false;
    }

    // LOG_TRACE("data: %s", json_data.c_str());
    if (m_zk_cli != nullptr) {
        m_zk_cli->set_payload_data(json_data);
    }
    return true;
}

bool Network::report_payload_to_manager() {
    if (!is_worker()) {
        LOG_ERROR("report payload to manager, only for worker!");
        return false;
    }

    m_payload.set_worker_index(worker_index());
    m_payload.set_cmd_cnt(0);
    m_payload.set_conn_cnt(m_conns.size() + m_node_conns.size());
    m_payload.set_create_time(now());

    if (!m_sys_cmd->send_payload_to_manager(m_payload)) {
        m_payload.Clear();
        return false;
    }

    m_payload.Clear();
    return true;
}

void* Network::co_handle_accept_nodes_conn(void* d) {
    co_enable_hook_sys();
    co_task_t* task = (co_task_t*)d;
    Connection* c = task->c;
    Network* net = (Network*)c->privdata();
    return net->handle_accept_nodes_conn(d);
}

void* Network::handle_accept_nodes_conn(void*) {
    Connection* c;
    char ip[NET_IP_STR_LEN];
    int fd, port, family, max = MAX_ACCEPTS_PER_CALL;

    while (max--) {
        fd = anet_tcp_accept(m_errstr, m_node_fd, ip, sizeof(ip), &port, &family);
        if (fd == ANET_ERR) {
            if (errno != EWOULDBLOCK) {
                LOG_ERROR("accepting client connection failed: fd: %d, errstr %s",
                          m_node_fd, m_errstr);
            }
            co_sleep(1000, m_node_fd, POLLIN);
            continue;
        }

        LOG_INFO("accepted server %s:%d, fd: %d", ip, port, fd);

        c = create_conn(fd, Codec::TYPE::PROTOBUF);
        if (c == nullptr) {
            close_fd(fd);
            LOG_ERROR("add data fd read event failed, fd: %d", fd);
            continue;
        }
        c->set_system(true);

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

void* Network::co_handle_accept_gate_conn(void* d) {
    co_enable_hook_sys();
    co_task_t* task = (co_task_t*)d;
    Connection* c = task->c;
    Network* net = (Network*)c->privdata();
    return net->handle_accept_gate_conn(d);
}

void* Network::handle_accept_gate_conn(void* d) {
    channel_t ch;
    char ip[NET_IP_STR_LEN] = {0};
    int fd, port, family, channel_fd, err;

    for (;;) {
        fd = anet_tcp_accept(m_errstr, m_gate_fd, ip, sizeof(ip), &port, &family);
        if (fd == ANET_ERR) {
            if (errno != EWOULDBLOCK) {
                LOG_WARN("accepting client connection: %s", m_errstr);
            }
            co_sleep(1000, m_gate_fd, POLLIN);
            continue;
        }

        LOG_INFO("accepted client: %s:%d, fd: %d", ip, port, fd);

        /* transfer fd from manager to worker. */
        channel_fd = m_worker_data_mgr->get_next_worker_data_fd();
        if (channel_fd <= 0) {
            LOG_ERROR("find next worker channel failed!");
            break;
        }

        ch = {fd, family, static_cast<int>(m_gate_codec), 0};

        for (;;) {
            err = write_channel(channel_fd, &ch, sizeof(channel_t), m_logger);
            if (err == ERR_OK) {
                LOG_DEBUG("send client fd: %d to worker through channel fd %d", fd, channel_fd);
                break;
            } else if (err == EAGAIN) {
                LOG_DEBUG("wait to write again, fd: %d, errno: %d", fd, err);
                co_sleep(1000, channel_fd, POLLOUT);
                continue;
            } else {
                LOG_ERROR("write channel failed! errno: %d", err);
                break;
            }
        }

        close_fd(fd);
    }

    return 0;
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
                // LOG_TRACE("read channel again next time! channel fd: %d", data_fd);
                co_sleep(1000, data_fd, POLLIN);
                continue;
            } else {
                LOG_CRIT("read channel failed, exit! channel fd: %d", data_fd);
                _exit(EXIT_FD_TRANSFER);
            }
        }

        if ((int)m_conns.size() > m_max_clients) {
            LOG_WARN("max number of clients reached! %d", m_max_clients);
            close_fd(ch.fd);
            co_sleep(10);
            continue;
        }

        codec = static_cast<Codec::TYPE>(ch.codec);
        c = create_conn(ch.fd, codec);
        if (c == nullptr) {
            close_fd(ch.fd);
            LOG_ERROR("add data fd read event failed, fd: %d", ch.fd);
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
    c = (Connection*)task->c;
    fd = c->fd();

    for (;;) {
        if (!is_valid_conn(c)) {
            LOG_ERROR("invalid conn: %p", c);
            break;
        }

        if (!c->is_system()) {
            /* check alive. */
            if (now() - c->active_time() > m_keep_alive) {
                LOG_DEBUG("conn timeout, fd: %d, now: %llu, active: %llu, keep alive: %llu\n",
                          fd, now(), c->active_time(), m_keep_alive);
                break;
            }
        }

        if (process_msg(c) != ERR_OK) {
            break;
        } else {
            co_sleep(1000, fd, POLLIN);
        }
    }

    close_conn(c);
    task->c = nullptr;
    m_coroutines->add_free_co_task(task);
    return 0;
}

int Network::process_msg(Connection* c) {
    return (c->is_http()) ? process_http_msg(c) : process_tcp_msg(c);
}

int Network::process_tcp_msg(Connection* c) {
    // LOG_TRACE("handle tcp msg, fd: %d", c->fd());

    int fd, ret;
    Request* req;
    Codec::STATUS stat;
    uint32_t old_cnt, old_bytes;

    fd = c->fd();
    ret = ERR_OK;
    old_cnt = c->read_cnt();
    old_bytes = c->read_bytes();
    req = new Request(c->ft());

    stat = c->conn_read(*req->msg_head(), *req->msg_body());
    if (stat != Codec::STATUS::ERR) {
        m_payload.set_read_cnt(m_payload.read_cnt() + (c->read_cnt() - old_cnt));
        m_payload.set_read_bytes(m_payload.read_bytes() + (c->read_bytes() - old_bytes));
    }

    // LOG_TRACE("conn read result, fd: %d, ret: %d", fd, (int)stat);

    while (stat == Codec::STATUS::OK) {
        ret = ERR_UNKOWN_CMD;

        if (c->is_system()) {
            ret = m_sys_cmd->handle_msg(req);
            if (ret != ERR_OK && ret != ERR_UNKOWN_CMD) {
                if (ret != ERR_TRANSFER_FD_DONE) {
                    LOG_ERROR("handle sys msg failed! fd: %d", req->fd());
                }
                break;
            }
        }

        if (ret == ERR_UNKOWN_CMD) {
            ret = m_module_mgr->handle_request(req);
            if (ret != ERR_OK) {
                if (ret == ERR_UNKOWN_CMD) {
                    LOG_WARN("can not find cmd handler! ret: %d, fd: %d, cmd: %d",
                             ret, fd, req->msg_head()->cmd());
                } else {
                    LOG_DEBUG("handler cmd failed! ret: %d, fd: %d, cmd: %d",
                              ret, fd, req->msg_head()->cmd());
                }
                break;
            }
        }

        req->msg_head()->Clear();
        req->msg_body()->Clear();

        stat = c->fetch_data(*req->msg_head(), *req->msg_body());
        LOG_TRACE("conn read result, fd: %d, ret: %d", fd, (int)stat);
    }

    if (stat == Codec::STATUS::ERR || stat == Codec::STATUS::CLOSED) {
        LOG_DEBUG("read failed! fd: %d", c->fd());
        ret = ERR_PACKET_DECODE_FAILED;
    }

    SAFE_DELETE(req);
    return ret;
}

int Network::process_http_msg(Connection* c) {
    return ERR_OK;
}

Connection* Network::create_conn(int fd, Codec::TYPE codec, bool is_channel) {
    if (anet_no_block(m_errstr, fd) != ANET_OK) {
        LOG_ERROR("set socket no block failed! fd: %d, errstr: %s", fd, m_errstr);
        return nullptr;
    }

    if (!is_channel) {
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
        LOG_ERROR("add channel event failed! fd: %d", fd);
        return nullptr;
    }
    c->init(codec);
    c->set_privdata(this);
    c->set_active_time(now());
    c->set_state(Connection::STATE::CONNECTED);

    if (is_channel) {
        c->set_system(true);
    }

    LOG_TRACE("create connection done! fd: %d", fd);
    return c;
}

Connection* Network::create_conn(int fd) {
    uint64_t id;
    Connection* c;

    id = new_seq();
    c = new Connection(m_logger, fd, id);
    if (c == nullptr) {
        LOG_ERROR("new connection failed! fd: %d", fd);
        return nullptr;
    }

    m_conns[id] = c;
    m_fd_conns[fd] = id;
    c->set_net(this);
    c->set_keep_alive(m_keep_alive);
    LOG_DEBUG("create connection fd: %d, id: %llu", fd, id);
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

    m_sys_cmd = new SysCmd(m_logger, this);
    if (m_sys_cmd == nullptr) {
        LOG_ERROR("alloc sys cmd failed!");
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
    m_zk_cli = new ZkClient(m_logger, this);
    if (m_zk_cli == nullptr) {
        LOG_ERROR("new zk mgr failed!");
        return false;
    }

    if (m_zk_cli->init(m_config)) {
        LOG_INFO("load zk client done!");
    }
    return true;
}

bool Network::load_nodes_conn() {
    m_nodes_conn = new NodeConn(this, m_logger);
    return true;
}

bool Network::load_mysql_mgr() {
    m_mysql_mgr = new MysqlMgr(m_logger);
    if (!m_mysql_mgr->init(&m_config["database"])) {
        LOG_ERROR("load database mgr failed!");
        return false;
    }

    LOG_INFO("load mysql pool done!");
    return true;
}

bool Network::load_redis_mgr() {
    m_redis_mgr = new RedisMgr(m_logger);
    if (!m_redis_mgr->init(&m_config["redis"])) {
        LOG_ERROR("load redis mgr failed!");
        return false;
    }

    LOG_INFO("load redis pool done!");
    return true;
}

int Network::send_to(Connection* c, const MsgHead& head, const MsgBody& body) {
    if (c == nullptr || c->is_invalid()) {
        return ERR_INVALID_CONN;
    }

    Codec::STATUS stat = c->conn_append_message(head, body);
    if (stat != Codec::STATUS::OK) {
        LOG_ERROR("encode message failed! fd: %d", c->fd());
        return ERR_ENCODE_DATA_FAILED;
    }

    for (;;) {
        if (!is_valid_conn(c)) {
            LOG_ERROR("invalid conn, fd: %d, id: %llu", c->fd(), c->id());
            return ERR_INVALID_CONN;
        }

        stat = conn_write_data(c);
        if (stat == Codec::STATUS::OK) {
            return ERR_OK;
        } else if (stat == Codec::STATUS::PAUSE) {
            co_sleep(100, c->fd(), POLLOUT);
            continue;
        } else {
            LOG_DEBUG("send data failed! fd: %d", c->fd());
            return ERR_SEND_DATA_FAILED;
        }
    }
}

Codec::STATUS Network::conn_write_data(Connection* c) {
    int old_cnt = c->write_cnt();
    int old_bytes = c->write_bytes();
    Codec::STATUS stat = c->conn_write();
    if (stat != Codec::STATUS::ERR) {
        m_payload.set_write_cnt(m_payload.write_cnt() + (c->write_cnt() - old_cnt));
        m_payload.set_write_bytes(m_payload.write_bytes() + (c->write_bytes() - old_bytes));
    }
    return stat;
}

Connection* Network::get_conn(const fd_t& ft) {
    auto it = m_conns.find(ft.id);
    if (it == m_conns.end()) {
        return nullptr;
    }

    Connection* c = it->second;
    if (c->fd() != ft.fd) {
        LOG_WARN("conflict conn data, old id: %llu, fd: %d, new id: %llu, fd: %d",
                 c->id(), c->fd(), ft.id, ft.id);
        return nullptr;
    }

    return c;
}

int Network::send_to(const fd_t& ft, const MsgHead& head, const MsgBody& body) {
    return send_to(get_conn(ft), head, body);
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

    return send_to(req->ft(), head, body);
}

int Network::send_req(Connection* c, uint32_t cmd, uint32_t seq, const std::string& data) {
    if (c == nullptr) {
        LOG_DEBUG("invalid connection!");
        return false;
    }

    MsgHead head;
    MsgBody body;
    body.set_data(data);
    head.set_cmd(cmd);
    head.set_seq(seq);
    head.set_len(body.ByteSizeLong());
    return send_to(c, head, body);
}

int Network::send_req(const fd_t& ft, uint32_t cmd, uint32_t seq, const std::string& data) {
    MsgHead head;
    MsgBody body;
    body.set_data(data);
    head.set_seq(seq);
    head.set_cmd(cmd);
    head.set_len(body.ByteSizeLong());
    return send_to(ft, head, body);
}

int Network::send_to_manager(int cmd, uint64_t seq, const std::string& data) {
    if (!is_worker()) {
        LOG_ERROR("send_to_parent only for worker!");
        return ERR_INVALID_PROCESS_TYPE;
    }

    auto it = m_conns.find(m_manager_fctrl.id);
    if (it == m_conns.end()) {
        LOG_ERROR("can not find manager ctrl fd, fd: %d", m_manager_fctrl.fd);
        return ERR_INVALID_CONN;
    }

    int ret = send_req(it->second, cmd, seq, data);
    if (ret != ERR_OK) {
        LOG_ALERT("send to parent failed! fd: %d", m_manager_fctrl.fd);
        return ret;
    }

    return ret;
}

int Network::send_to_workers(int cmd, uint64_t seq, const std::string& data) {
    LOG_TRACE("send to children, cmd: %d, seq: %llu", cmd, seq);

    if (!is_manager()) {
        LOG_ERROR("send_to_children only for manager!");
        return ERR_INVALID_PROCESS_TYPE;
    }

    /* manger and workers communicate through socketpair. */
    const std::unordered_map<int, worker_info_t*>& workers =
        m_worker_data_mgr->get_infos();

    for (auto& v : workers) {
        auto it = m_conns.find(v.second->fctrl.id);
        if (it == m_conns.end() || it->second->is_invalid()) {
            LOG_ALERT("ctrl fd is invalid! fd: %d", v.second->fctrl.fd);
            continue;
        }

        if (send_req(it->second, cmd, seq, data) != ERR_OK) {
            LOG_ALERT("send to worker failed! fd: %d", v.second->fctrl.fd);
            continue;
        }
    }

    return ERR_OK;
}

void Network::clear_routines() {
    SAFE_DELETE(m_coroutines);
    FreeLibcoEnv();
}

void Network::destory() {
    exit_libco();
    SAFE_DELETE(m_zk_cli);
    SAFE_DELETE(m_module_mgr);
    SAFE_DELETE(m_mysql_mgr);
    SAFE_DELETE(m_sys_cmd);
    SAFE_DELETE(m_redis_mgr);
    SAFE_DELETE(m_nodes_conn);
    SAFE_DELETE(m_worker_data_mgr);
    SAFE_DELETE(m_nodes);
    close_fds();
    clear_routines();
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
    m_node_conns.clear();
}

void Network::close_channel(int* fds) {
    LOG_DEBUG("close channel, fd0: %d, fd1: %d", fds[0], fds[1]);
    close_fd(fds[0]);
    close_fd(fds[1]);
}

bool Network::close_conn(Connection* c) {
    if (c == nullptr) {
        return false;
    }
    return close_conn(c->id());
}

bool Network::close_conn(uint64_t id) {
    LOG_DEBUG("close conn, id: %llu", id);

    auto it = m_conns.find(id);
    if (it == m_conns.end()) {
        return false;
    }

    Connection* c = it->second;
    c->set_state(Connection::STATE::CLOSED);

    if (!c->get_node_id().empty()) {
        m_node_conns.erase(c->get_node_id());
    }

    LOG_DEBUG("close conn, fd: %d, id: %llu", c->fd(), c->id());

    m_conns.erase(it);
    auto itr = m_fd_conns.find(c->fd());
    if (itr != m_fd_conns.end() && itr->second == c->ft().id) {
        LOG_DEBUG("remove fdt done! fd: %d, id: %llu", c->fd(), c->id());
        m_fd_conns.erase(c->fd());
        close_fd(c->fd());
    } else {
        LOG_WARN("remove fdt failed, fd: %d, id: %llu", c->fd(), c->id());
    }

    SAFE_DELETE(c);
    return true;
}

bool Network::is_valid_conn(Connection* c) {
    if (c == nullptr) {
        return false;
    }
    return is_valid_conn(c->id());
}

bool Network::is_valid_conn(uint64_t id) {
    auto it = m_conns.find(id);
    if (it == m_conns.end()) {
        LOG_WARN("can not find client id: %d", id);
        return false;
    }

    Connection* c = it->second;
    if (c->is_invalid()) {
        return false;
    }

    auto itr = m_fd_conns.find(c->fd());
    if (itr == m_fd_conns.end()) {
        LOG_WARN("can not find conn fd, fd: %d", c->fd());
        return false;
    }

    if (itr->second != c->id()) {
        LOG_WARN("invalid conn id, new: %llu, old: %llu", itr->second, c->id());
        return false;
    }

    return true;
}

void Network::close_fd(int fd) {
    LOG_DEBUG("close fd: %d.", fd);

    if (close(fd) == -1) {
        LOG_WARN("close channel failed, fd: %d. errno: %d, errstr: %s",
                 fd, errno, strerror(errno));
    }
    LOG_DEBUG("close fd: %d.", fd);
}

bool Network::set_gate_codec(const std::string& codec_type) {
    Codec::TYPE type = Codec::get_codec_type(codec_type);
    if (type != Codec::TYPE::UNKNOWN) {
        m_gate_codec = type;
        return true;
    }
    return false;
}

bool Network::update_conn_state(const fd_t& ft, int state) {
    auto it = m_conns.find(ft.id);
    if (it == m_conns.end()) {
        return false;
    }
    it->second->set_state((Connection::STATE)state);
    return true;
}

bool Network::add_client_conn(const std::string& node_id, const fd_t& ft) {
    Connection* c = get_conn(ft);
    if (c == nullptr) {
        return false;
    }
    m_node_conns[node_id] = c;
    return true;
}

int Network::relay_to_node(const std::string& node_type, const std::string& obj,
                           MsgHead* head, MsgBody* body, MsgHead* head_out, MsgBody* body_out) {
    if (!is_worker()) {
        LOG_ERROR("relay_to_node only for worker!");
        return ERR_INVALID_PROCESS_TYPE;
    }

    LOG_DEBUG("relay to node, type: %s, obj: %s", node_type.c_str(), obj.c_str());

    return m_nodes_conn->relay_to_node(node_type, obj, head, body, head_out, body_out);
}

uint64_t Network::now(bool force) {
    if (force) {
        m_now_time = mstime();
    } else {
        if ((++m_time_index % 10) == 0) {
            /* mstime waste too much cpu, so set it at intervals and in timer. */
            m_now_time = mstime();
        }
    }
    return m_now_time;
}

}  // namespace kim