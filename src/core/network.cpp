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

Network::Network(std::shared_ptr<Log> logger, TYPE type) : Logger(logger), m_type(type) {
    m_now_time = mstime();
}

Network::~Network() {
    destroy();
}

/* parent. */
bool Network::create_m(std::shared_ptr<SysConfig> config) {
    if (config == nullptr) {
        return false;
    }

    m_config = config;

    if (!load_public(config)) {
        LOG_ERROR("load public failed!");
        return false;
    }

    if (!load_worker_data_mgr()) {
        LOG_ERROR("new worker data mgr failed!");
        return false;
    }

    if (m_config->is_open_zookeeper()) {
        if (!load_zk_mgr()) {
            LOG_ERROR("load zookeeper mgr failed!");
            return false;
        }
    }

    /* inner listen. */
    if (!config->node_host().empty()) {
        auto fd = listen_to_port(config->node_host().c_str(), config->node_port());
        if (fd == -1) {
            LOG_ERROR("listen to port failed! %s:%d",
                      config->node_host().c_str(), config->node_port());
            return false;
        }

        m_node_fd = fd;
        LOG_INFO("node fd: %d", m_node_fd);

        auto c = create_conn(m_node_fd, Codec::TYPE::PROTOBUF);
        if (c == nullptr) {
            close_fd(m_node_fd);
            LOG_ERROR("add read event failed, fd: %d", m_node_fd);
            return false;
        }

        auto co = m_coroutines->start_co(
            [this](void* arg) {
                on_handle_accept_nodes_conn();
                m_coroutines->add_free_co((stCoRoutine_t*)arg);
            });
        if (co == nullptr) {
            LOG_ERROR("create new corotines failed!");
            close_conn(c);
            return false;
        }
    }

    /* gate listen. */
    if (!config->is_reuseport()) {
        if (!config->gate_host().empty()) {
            auto fd = listen_to_port(config->gate_host().c_str(), config->gate_port());
            if (fd == -1) {
                LOG_ERROR("listen to gate failed! %s:%d",
                          config->gate_host().c_str(), config->gate_port());
                return false;
            }

            m_gate_fd = fd;
            LOG_INFO("gate fd: %d, host: %s, port: %d",
                     m_gate_fd, config->gate_host().c_str(), config->gate_port());

            auto c = create_conn(m_gate_fd, m_gate_codec);
            if (c == nullptr) {
                close_fd(m_gate_fd);
                LOG_ERROR("add read event failed, fd: %d", m_gate_fd);
                return false;
            }

            auto co = m_coroutines->start_co([this](void* arg) {
                on_handle_accept_gate_conn();
                m_coroutines->add_free_co((stCoRoutine_t*)arg);
            });
            if (co == nullptr) {
                LOG_ERROR("create new corotines failed!");
                close_conn(c);
                return false;
            }
        }
    }

    return true;
}

bool Network::init_manager_channel(fd_t& fctrl, fd_t& fdata) {
    if (!is_manager()) {
        return false;
    }

    auto conn_ctrl = create_conn(fctrl.fd, Codec::TYPE::PROTOBUF, true);
    if (conn_ctrl == nullptr) {
        close_fd(fctrl.fd);
        LOG_ERROR("add read event failed, fd: %d", fctrl.fd);
        return false;
    }

    auto conn_data = create_conn(fdata.fd, Codec::TYPE::PROTOBUF, true);
    if (conn_data == nullptr) {
        close_fd(fdata.fd);
        close_conn(conn_ctrl);
        LOG_ERROR("add read event failed, fd: %d", fdata.fd);
        return false;
    }

    fctrl = m_manager_fctrl = conn_ctrl->ft();
    fdata = m_manager_fdata = conn_data->ft();

    /* recv data from worker. */
    auto co = m_coroutines->start_co(
        [this, conn_ctrl](void* arg) {
            on_handle_requests(conn_ctrl);
            m_coroutines->add_free_co((stCoRoutine_t*)arg);
        });
    if (co == nullptr) {
        LOG_ERROR("create new corotines failed!");
        close_conn(conn_ctrl);
        close_conn(conn_data);
        return false;
    }

    return true;
}

/* worker. */
bool Network::create_w(std::shared_ptr<SysConfig> config, int ctrl_fd, int data_fd, int index) {
    if (config == nullptr) {
        return false;
    }

    m_config = config;
    m_worker_index = index;

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

    auto conn_ctrl = create_conn(ctrl_fd, Codec::TYPE::PROTOBUF, true);
    if (conn_ctrl == nullptr) {
        LOG_ERROR("add read event failed, fd: %d", ctrl_fd);
        return false;
    }

    auto conn_data = create_conn(data_fd, Codec::TYPE::PROTOBUF, true);
    if (conn_data == nullptr) {
        LOG_ERROR("add read event failed, fd: %d", data_fd);
        return false;
    }

    m_manager_fctrl = conn_ctrl->ft();
    m_manager_fdata = conn_data->ft();

    auto co = m_coroutines->start_co(
        [this, data_fd](void* arg) {
            on_handle_read_transfer_fd(data_fd);
            m_coroutines->add_free_co((stCoRoutine_t*)arg);
        });
    if (co == nullptr) {
        LOG_ERROR("create new corotines failed!");
        close_conn(conn_data);
        return false;
    }

    co = m_coroutines->start_co(
        [this, conn_ctrl](void* arg) {
            on_handle_requests(conn_ctrl);
            m_coroutines->add_free_co((stCoRoutine_t*)arg);
        });
    if (co == nullptr) {
        LOG_ERROR("create new corotines failed!");
        close_conn(conn_ctrl);
        return false;
    }

    /* gate listen. */
    if (config->is_reuseport()) {
        if (!config->gate_host().empty()) {
            auto fd = listen_to_port(config->gate_host().c_str(), config->gate_port(), true);
            if (fd == -1) {
                LOG_ERROR("listen to gate failed! %s:%d",
                          config->gate_host().c_str(), config->gate_port());
                return false;
            }

            m_gate_fd = fd;
            LOG_INFO("gate fd: %d, host: %s, port: %d",
                     fd, config->gate_host().c_str(), config->gate_port());

            auto c = create_conn(fd, m_gate_codec);
            if (c == nullptr) {
                close_fd(fd);
                LOG_ERROR("add read event failed, fd: %d", fd);
                return false;
            }

            auto co = m_coroutines->start_co(
                [this](void* arg) {
                    on_handle_accept_gate_conn();
                    m_coroutines->add_free_co((stCoRoutine_t*)arg);
                });
            if (co == nullptr) {
                LOG_ERROR("create new corotines failed!");
                close_conn(c);
                return false;
            }
        }
    }

    LOG_INFO("create network done!");
    return true;
}

bool Network::ensure_files_limit() {
    auto max_clients = m_config->max_clients();
    if (max_clients == 0) {
        return false;
    }

    int error = 0;
    auto file_limit = adjust_files_limit(max_clients + CONFIG_MIN_RESERVED_FDS, error);
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

int Network::listen_to_port(const char* host, int port, bool is_reuseport) {
    auto fd = anet_tcp_server(m_errstr, host, port, TCP_BACK_LOG, is_reuseport);
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

#if !defined(__APPLE__)
    /* glibc maybe memory leak, so release the cache memory in timer. */
    run_with_period(60 * 1000) {
        malloc_trim(0);
    }
#endif

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

    PayloadStats pls;
    std::string json_data;
    int cmd_cnt = 0, conn_cnt = 0, read_cnt = 0, write_cnt = 0, read_bytes = 0, write_bytes = 0;
    const auto& workers = m_worker_data_mgr->get_infos();

    /* node info. */
    auto node = pls.mutable_node();
    node->set_zk_path(m_nodes->get_my_zk_node_path());
    node->set_node_type(node_type());
    node->set_node_host(node_host());
    node->set_node_port(node_port());
    node->set_gate_host(m_config->gate_host());
    node->set_gate_port(m_config->gate_port());
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
        auto worker_pl = pls.add_workers();
        *worker_pl = info->payload;
        if (info->payload.worker_index() == 0) {
            worker_pl->set_worker_index(info->index);
        }
    }

    /* manager statistics data results。 */
    auto manager_pl = pls.mutable_manager();
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

void Network::on_handle_accept_nodes_conn() {
    co_enable_hook_sys();

    char ip[NET_IP_STR_LEN];
    int port, family, max = MAX_ACCEPTS_PER_CALL;

    while (max--) {
        auto fd = anet_tcp_accept(m_errstr, m_node_fd, ip, sizeof(ip), &port, &family);
        if (fd == ANET_ERR) {
            if (errno != EWOULDBLOCK) {
                LOG_ERROR("accepting client connection failed: fd: %d, errstr %s",
                          m_node_fd, m_errstr);
            }
            co_sleep(10000, m_node_fd, POLLIN);
            continue;
        }

        LOG_INFO("accepted server %s:%d, fd: %d", ip, port, fd);

        auto c = create_conn(fd, Codec::TYPE::PROTOBUF);
        if (c == nullptr) {
            close_fd(fd);
            LOG_ERROR("add data fd read event failed, fd: %d", fd);
            continue;
        }
        c->set_system(true);

        auto co = m_coroutines->start_co(
            [this, c](void* arg) {
                on_handle_requests(c);
                m_coroutines->add_free_co((stCoRoutine_t*)arg);
            });
        if (co == nullptr) {
            LOG_ERROR("create new corotines failed!");
            close_conn(c);
            continue;
        }
    }
}

void Network::on_handle_accept_gate_conn() {
    co_enable_hook_sys();

    channel_t ch;
    char ip[NET_IP_STR_LEN] = {0};
    int port, family, channel_fd;

    for (;;) {
        auto fd = anet_tcp_accept(m_errstr, m_gate_fd, ip, sizeof(ip), &port, &family);
        if (fd == ANET_ERR) {
            if (errno != EWOULDBLOCK) {
                LOG_WARN("accepting client connection: %s", m_errstr);
            }
            co_sleep(10000, m_gate_fd, POLLIN);
            continue;
        }

        LOG_INFO("accepted client: %s:%d, fd: %d", ip, port, fd);

        if (!m_config->is_reuseport()) {
            /* transfer fd from manager to worker. */
            channel_fd = m_worker_data_mgr->get_next_worker_data_fd();
            if (channel_fd <= 0) {
                LOG_ERROR("find next worker channel failed!");
                break;
            }

            ch = {fd, family, static_cast<int>(m_gate_codec), 0};

            for (;;) {
                auto err = write_channel(channel_fd, &ch, sizeof(channel_t), logger());
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
        } else {
            if ((int)m_conns.size() > m_max_clients) {
                LOG_WARN("max number of clients reached! %d", m_max_clients);
                close_fd(fd);
                co_sleep(10);
                continue;
            }

            auto c = create_conn(fd, m_gate_codec);
            if (c == nullptr) {
                close_fd(fd);
                LOG_ERROR("add data fd read event failed, fd: %d", fd);
                continue;
            }

            LOG_INFO("accept client: fd: %d", fd);

            auto co = m_coroutines->start_co(
                [this, c](void* arg) {
                    on_handle_requests(c);
                    m_coroutines->add_free_co((stCoRoutine_t*)arg);
                });
            if (co == nullptr) {
                LOG_ERROR("create new corotines failed!");
                close_conn(c);
                continue;
            }
        }
    }
}

void Network::on_handle_read_transfer_fd(int fd) {
    co_enable_hook_sys();
    LOG_TRACE("on_handle_read_transfer_fd....");

    channel_t ch;

    for (;;) {
        /* read fd from parent. */
        auto err = read_channel(fd, &ch, sizeof(channel_t), logger());
        if (err != 0) {
            if (err == EAGAIN) {
                // LOG_TRACE("read channel again next time! channel fd: %d", fd);
                co_sleep(10000, fd, POLLIN);
                continue;
            } else {
                LOG_CRIT("read channel failed, exit! channel fd: %d", fd);
                _exit(EXIT_FD_TRANSFER);
            }
        }

        if ((int)m_conns.size() > m_max_clients) {
            LOG_WARN("max number of clients reached! %d", m_max_clients);
            close_fd(ch.fd);
            co_sleep(10);
            continue;
        }

        auto codec = static_cast<Codec::TYPE>(ch.codec);
        auto c = create_conn(ch.fd, codec);
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

        auto co = m_coroutines->start_co(
            [this, c](void* arg) {
                on_handle_requests(c);
                m_coroutines->add_free_co((stCoRoutine_t*)arg);
            });
        if (co == nullptr) {
            LOG_ERROR("create new corotines failed!");
            close_conn(c);
            continue;
        }
    }
}

void Network::on_handle_requests(std::shared_ptr<Connection> c) {
    co_enable_hook_sys();

    for (;;) {
        if (!is_valid_conn(c)) {
            LOG_ERROR("invalid conn, id: %llu, fd: %d", c->id(), c->fd());
            break;
        }

        if (!c->is_system()) {
            /* check alive. */
            if (now() - c->active_time() > m_keep_alive) {
                LOG_DEBUG("conn timeout, fd: %d, now: %llu, active: %llu, keep alive: %llu\n",
                          c->fd(), now(), c->active_time(), m_keep_alive);
                break;
            }
        }

        if (process_msg(c) != ERR_OK) {
            break;
        } else {
            co_sleep(1000, c->fd(), POLLIN);
        }
    }

    close_conn(c);
}

int Network::process_msg(std::shared_ptr<Connection> c) {
    return (c->is_http()) ? process_http_msg(c) : process_tcp_msg(c);
}

int Network::process_tcp_msg(std::shared_ptr<Connection> c) {
    // LOG_TRACE("handle tcp msg, fd: %d", c->fd());
    int fd = c->fd();
    int ret = ERR_OK;
    auto old_cnt = c->read_cnt();
    auto old_bytes = c->read_bytes();

    auto req = std::make_shared<Request>(c->ft());
    auto status = c->conn_read(*req->msg_head(), *req->msg_body());
    if (status != Codec::STATUS::ERR) {
        m_payload.set_read_cnt(m_payload.read_cnt() + (c->read_cnt() - old_cnt));
        m_payload.set_read_bytes(m_payload.read_bytes() + (c->read_bytes() - old_bytes));
    }

    // LOG_TRACE("conn read result, fd: %d, ret: %d", fd, (int)status);

    while (status == Codec::STATUS::OK) {
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
            if (is_worker()) {
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
            } else {
                send_ack(req, ERR_UNKOWN_CMD, "invalid cmd!");
                break;
            }
        }

        req->msg_head()->Clear();
        req->msg_body()->Clear();

        status = c->fetch_data(*req->msg_head(), *req->msg_body());
        LOG_TRACE("conn read result, fd: %d, ret: %d", fd, (int)status);
    }

    if (status == Codec::STATUS::ERR || status == Codec::STATUS::CLOSED) {
        LOG_DEBUG("read failed! fd: %d", c->fd());
        ret = ERR_PACKET_DECODE_FAILED;
    }

    return ret;
}

int Network::process_http_msg(std::shared_ptr<Connection> c) {
    return ERR_OK;
}

std::shared_ptr<Connection> Network::create_conn(int fd, Codec::TYPE codec, bool is_channel) {
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

    auto c = create_conn(fd);
    if (c == nullptr) {
        LOG_ERROR("add channel event failed! fd: %d", fd);
        return nullptr;
    }
    c->init(codec);
    // c->set_privdata(this);
    c->set_active_time(now());
    c->set_state(Connection::STATE::CONNECTED);

    if (is_channel) {
        c->set_system(true);
    }

    LOG_TRACE("create connection done! fd: %d", fd);
    return c;
}

std::shared_ptr<Connection> Network::create_conn(int fd) {
    auto id = new_seq();
    auto c = std::make_shared<Connection>(logger(), shared_from_this(), fd, id);
    if (c == nullptr) {
        LOG_ERROR("new connection failed! fd: %d", fd);
        return nullptr;
    }

    m_conns[id] = c;
    m_fd_conns[fd] = id;
    c->set_keep_alive(m_keep_alive);
    LOG_DEBUG("create connection fd: %d, id: %llu", fd, id);
    return c;
}

bool Network::load_public(std::shared_ptr<SysConfig> config) {
    if (!load_config(config)) {
        LOG_ERROR("load config failed!");
        return false;
    }

    if (!load_corotines()) {
        LOG_ERROR("load coroutines failed!");
        return false;
    }

    m_sys_cmd = std::make_shared<SysCmd>(logger(), shared_from_this());
    if (m_sys_cmd == nullptr) {
        LOG_ERROR("alloc sys cmd failed!");
        return false;
    }

    m_nodes = std::make_shared<Nodes>(logger());
    if (m_nodes == nullptr) {
        LOG_ERROR("alloc nodes failed!");
        return false;
    }

    if (!load_session_mgr()) {
        LOG_ERROR("alloc session mgr failed!");
        return false;
    }

    LOG_INFO("load public done!");
    return true;
}

bool Network::load_modules() {
    m_module_mgr = std::unique_ptr<ModuleMgr>(new ModuleMgr(logger(), shared_from_this()));
    if (m_module_mgr == nullptr) {
        LOG_ERROR("alloc modules mgr failed!");
        return false;
    }

    if (!m_module_mgr->init(m_config->config())) {
        LOG_ERROR("modules mgr init failed!");
        return false;
    }

    LOG_INFO("load modules mgr sucess!");
    return true;
}

bool Network::load_corotines() {
    m_coroutines = std::unique_ptr<Coroutines>(new Coroutines(logger()));
    if (m_coroutines == nullptr) {
        LOG_ERROR("alloc coroutines failed!");
        return false;
    }
    return true;
}

bool Network::load_config(std::shared_ptr<SysConfig> config) {
    auto codec = config->gate_codec();
    if (!codec.empty()) {
        if (!set_gate_codec(codec)) {
            LOG_ERROR("invalid codec: %s", codec.c_str());
            return false;
        }
        LOG_DEBUG("gate codec: %s", codec.c_str());
    }

    auto secs = config->keep_alive();
    if (secs > 0) {
        set_keep_alive(secs);
    }

    if (config->node_type().empty()) {
        LOG_ERROR("invalid inner node info!");
        return false;
    }

    return true;
}

bool Network::load_worker_data_mgr() {
    m_worker_data_mgr = std::make_shared<WorkerDataMgr>(logger());
    if (m_worker_data_mgr == nullptr) {
        LOG_ERROR("load worker data mgr failed!");
        return false;
    }
    return true;
}

bool Network::load_zk_mgr() {
    m_zk_cli = std::make_shared<ZkClient>(logger(), shared_from_this());
    if (m_zk_cli == nullptr) {
        LOG_ERROR("new zk mgr failed!");
        return false;
    }

    if (!m_zk_cli->init(m_config->config())) {
        LOG_ERROR("load zk client failed!");
        return false;
    }

    LOG_INFO("load zk client done!");
    return true;
}

bool Network::load_nodes_conn() {
    m_nodes_conn = std::unique_ptr<NodeConn>(new NodeConn(shared_from_this(), logger()));
    if (m_nodes_conn == nullptr) {
        LOG_ERROR("alloc nodes conn failed!");
        return true;
    }
    return true;
}

bool Network::load_mysql_mgr() {
    m_mysql_mgr = std::make_shared<MysqlMgr>(logger());
    if (m_mysql_mgr == nullptr) {
        LOG_ERROR("alloc mysql mgr failed!");
        return false;
    }

    if (!m_mysql_mgr->init(&(*m_config->config())["database"])) {
        LOG_ERROR("load database mgr failed!");
        return false;
    }

    LOG_INFO("load mysql pool done!");
    return true;
}

bool Network::load_redis_mgr() {
    m_redis_mgr = std::make_shared<RedisMgr>(logger());
    if (m_redis_mgr == nullptr) {
        LOG_ERROR("alloc redis mgr failed!");
        return false;
    }

    if (!m_redis_mgr->init(&(*m_config->config())["redis"])) {
        LOG_ERROR("load redis mgr failed!");
        return false;
    }

    LOG_INFO("load redis pool done!");
    return true;
}

bool Network::load_session_mgr() {
    m_session_mgr = std::make_shared<SessionMgr>(logger(), shared_from_this());
    if (!m_session_mgr->init()) {
        LOG_ERROR("init session mgr failed!\n");
        return false;
    }
    return true;
}

int Network::send_to(std::shared_ptr<Connection> c, const MsgHead& head, const MsgBody& body) {
    if (c == nullptr || c->is_invalid()) {
        return ERR_INVALID_CONN;
    }

    auto status = c->conn_append_message(head, body);
    if (status != Codec::STATUS::OK) {
        LOG_ERROR("encode message failed! fd: %d", c->fd());
        return ERR_ENCODE_DATA_FAILED;
    }

    for (;;) {
        if (!is_valid_conn(c)) {
            LOG_ERROR("invalid conn, fd: %d, id: %llu", c->fd(), c->id());
            return ERR_INVALID_CONN;
        }

        status = conn_write_data(c);
        if (status == Codec::STATUS::OK) {
            return ERR_OK;
        } else if (status == Codec::STATUS::PAUSE) {
            co_sleep(100, c->fd(), POLLOUT);
            continue;
        } else {
            LOG_DEBUG("send data failed! fd: %d", c->fd());
            return ERR_SEND_DATA_FAILED;
        }
    }
}

Codec::STATUS Network::conn_write_data(std::shared_ptr<Connection> c) {
    auto old_cnt = c->write_cnt();
    auto old_bytes = c->write_bytes();
    auto status = c->conn_write();
    if (status != Codec::STATUS::ERR) {
        m_payload.set_write_cnt(m_payload.write_cnt() + (c->write_cnt() - old_cnt));
        m_payload.set_write_bytes(m_payload.write_bytes() + (c->write_bytes() - old_bytes));
    }
    return status;
}

std::shared_ptr<Connection> Network::get_conn(const fd_t& ft) {
    auto it = m_conns.find(ft.id);
    if (it == m_conns.end()) {
        return nullptr;
    }

    auto c = it->second;
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

int Network::send_ack(std::shared_ptr<Request> req,
                      int err, const std::string& errstr, const std::string& data) {
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

int Network::send_req(std::shared_ptr<Connection> c, uint32_t cmd, uint32_t seq, const std::string& data) {
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
        LOG_ALERT("send to parent failed:1! fd: %d", m_manager_fctrl.fd);
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
    const auto& workers = m_worker_data_mgr->get_infos();
    for (const auto& v : workers) {
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
    m_coroutines->destroy();
    FreeLibcoEnv();
}

void Network::destroy() {
    exit_libco();
    close_fds();
    clear_routines();
}

void Network::close_fds() {
    for (const auto& it : m_conns) {
        std::shared_ptr<Connection> c = it.second;
        if (c && !c->is_invalid()) {
            close_fd(c->fd());
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

bool Network::close_conn(std::shared_ptr<Connection> c) {
    if (c == nullptr) {
        return false;
    }
    return close_conn(c->id());
}

bool Network::close_conn(uint64_t id) {
    auto it = m_conns.find(id);
    if (it == m_conns.end()) {
        return false;
    }

    auto c = it->second;
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

    return true;
}

bool Network::is_valid_conn(std::shared_ptr<Connection> c) {
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

    std::shared_ptr<Connection> c = it->second;
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
    auto c = get_conn(ft);
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