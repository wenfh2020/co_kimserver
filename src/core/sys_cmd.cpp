#include "sys_cmd.h"

#include "net/channel.h"
#include "protocol.h"
#include "worker_data_mgr.h"

namespace kim {

SysCmd::SysCmd(Log* logger, INet* net)
    : Logger(logger), m_net(net) {
}

/* auto_send(...)
 * A1 contact with B1. (auto_send func)
 * 
 * A1: node A's worker.
 * B0: node B's manager.
 * B1: node B's worker.
 * 
 * process_sys_message(.)
 * 1. A1 connect to B0. (inner host : inner port)
 * 2. A1 send CMD_REQ_CONNECT_TO_WORKER to B0.
 * 3. B0 send CMD_RSP_CONNECT_TO_WORKER to A1.
 * 4. B0 transfer A1's fd to B1.
 * 5. A1 send CMD_REQ_TELL_WORKER to B1.
 * 6. B1 send CMD_RSP_TELL_WORKER A1.
 * 7. A1 send waiting buffer to B1.
 * 8. B1 send ack to A1.
 */
int SysCmd::handle_msg(const Request* req) {
    if (req == nullptr) {
        return ERR_INVALID_PARAMS;
    }

    /* system cmd < 1000. */
    if (req->msg_head()->cmd() >= CMD_SYS_END) {
        return ERR_UNKOWN_CMD;
    }

    return (m_net->is_manager()) ? handle_manager_msg(req) : handle_worker_msg(req);
}

/* A1 contact with B1. */
int SysCmd::send_connect_req_to_worker(Connection* c) {
    LOG_DEBUG("send CMD_REQ_CONNECT_TO_WORKER, fd: %d", c->fd());

    std::string node_id;
    int ret, worker_index;

    node_id = c->get_node_id();
    worker_index = m_net->nodes()->get_node_worker_index(node_id);
    if (worker_index == -1) {
        LOG_ERROR("no node info! node id: %s", node_id.c_str())
        return ERR_INVALID_WORKER_INDEX;
    }

    ret = m_net->send_req(c, CMD_REQ_CONNECT_TO_WORKER, m_net->new_seq(), std::to_string(worker_index));
    if (ret != ERR_OK) {
        LOG_ERROR("send CMD_REQ_CONNECT_TO_WORKER failed! fd: %d", c->fd());
        return ret;
    }

    c->set_state(Connection::STATE::CONNECTING);
    return ERR_OK;
}

int SysCmd::send_payload_to_manager(const Payload& pl) {
    LOG_TRACE("send CMD_REQ_UPDATE_PAYLOAD");
    int ret = m_net->send_to_manager(CMD_REQ_UPDATE_PAYLOAD, m_net->new_seq(), pl.SerializeAsString());
    if (ret != ERR_OK) {
        LOG_ERROR("send CMD_REQ_UPDATE_PAYLOAD failed!");
    }
    return ret;
}

int SysCmd::handle_manager_msg(const Request* req) {
    LOG_TRACE("process manager message.");
    switch (req->msg_head()->cmd()) {
        case CMD_REQ_CONNECT_TO_WORKER: {
            return on_req_connect_to_worker(req);
        }
        case CMD_RSP_ADD_ZK_NODE: {
            return on_rsp_add_zk_node(req);
        }
        case CMD_RSP_DEL_ZK_NODE: {
            return on_rsp_del_zk_node(req);
        }
        case CMD_RSP_REGISTER_NODE: {
            return on_rsp_reg_zk_node(req);
        }
        case CMD_REQ_SYNC_ZK_NODES: {
            return on_req_sync_zk_nodes(req);
        }
        case CMD_REQ_UPDATE_PAYLOAD: {
            return on_req_update_payload(req);
        }
        default: {
            return ERR_UNKOWN_CMD;
        }
    }
}

int SysCmd::handle_worker_msg(const Request* req) {
    /* worker. */
    LOG_TRACE("process worker's msg, head cmd: %d, seq: %u",
              req->msg_head()->cmd(), req->msg_head()->seq());

    switch (req->msg_head()->cmd()) {
        case CMD_RSP_CONNECT_TO_WORKER: {
            return on_rsp_connect_to_worker(req);
        }
        case CMD_REQ_TELL_WORKER: {
            return on_req_tell_worker(req);
        }
        case CMD_RSP_TELL_WORKER: {
            return on_rsp_tell_worker(req);
        }
        case CMD_REQ_ADD_ZK_NODE: {
            return on_req_add_zk_node(req);
        }
        case CMD_REQ_DEL_ZK_NODE: {
            return on_req_del_zk_node(req);
        }
        case CMD_REQ_REGISTER_NODE: {
            return on_req_reg_zk_node(req);
        }
        case CMD_RSP_SYNC_ZK_NODES: {
            return on_rsp_sync_zk_nodes(req);
        }
        case CMD_RSP_UPDATE_PAYLOAD: {
            return on_rsp_update_payload(req);
        }
        default: {
            return ERR_UNKOWN_CMD;
        }
    }
}

int SysCmd::on_req_update_payload(const Request* req) {
    LOG_TRACE("handle CMD_REQ_UPDATE_PAYLOAD. fd: % d", req->fd());

    int ret;
    kim::Payload pl;
    worker_info_t* info;

    if (!pl.ParseFromString(req->msg_body()->data())) {
        LOG_ERROR("parse CMD_REQ_UPDATE_PAYLOAD data failed! fd: %d", req->fd());
        return ERR_INVALID_PROTOBUF_PACKET;
    }

    info = m_net->worker_data_mgr()->get_worker_info(pl.worker_index());
    if (info == nullptr) {
        m_net->send_ack(req, ERR_INVALID_WORKER_INDEX, "can not find worker index!");
        LOG_ERROR("can not find worker index: %d", pl.worker_index());
        return ERR_INVALID_WORKER_INDEX;
    }

    info->payload = pl;

    ret = m_net->send_ack(req, ERR_OK, "ok");
    if (ret != ERR_OK) {
        LOG_ERROR("send CMD_RSP_UPDATE_PAYLOAD failed! fd: %d", req->fd());
        return ret;
    }

    return ERR_OK;
}

int SysCmd::on_rsp_update_payload(const Request* req) {
    LOG_TRACE("CMD_RSP_UPDATE_PAYLOAD, fd: %d", req->fd());
    int ret = check_rsp(req);
    if (ret != ERR_OK) {
        LOG_ERROR("CMD_RSP_UPDATE_PAYLOAD is not ok! fd: %d", req->fd());
    }
    return ret;
}

int SysCmd::send_add_zk_node_to_worker(const zk_node& node) {
    LOG_TRACE("send CMD_REQ_ADD_ZK_NODE");
    return m_net->send_to_worker(CMD_REQ_ADD_ZK_NODE, m_net->new_seq(), node.SerializeAsString());
}

int SysCmd::send_del_zk_node_to_worker(const std::string& zk_path) {
    LOG_TRACE("send CMD_REQ_DEL_ZK_NODE, path: %d", zk_path.c_str());
    return m_net->send_to_worker(CMD_REQ_DEL_ZK_NODE, m_net->new_seq(), zk_path);
}

int SysCmd::send_reg_zk_node_to_worker(const register_node& rn) {
    LOG_TRACE("send CMD_REQ_REGISTER_NODE, path: %s", rn.my_zk_path().c_str());
    return m_net->send_to_worker(CMD_REQ_REGISTER_NODE, m_net->new_seq(), rn.SerializeAsString());
}

int SysCmd::send_zk_nodes_version_to_manager(int version) {
    LOG_TRACE("send CMD_REQ_SYNC_ZK_NODES");

    int ret = m_net->send_to_manager(CMD_REQ_SYNC_ZK_NODES, m_net->new_seq(), std::to_string(version));
    if (ret != ERR_OK) {
        LOG_ERROR("send CMD_REQ_SYNC_ZK_NODES failed!");
        return false;
    }
    return true;
}

int SysCmd::on_rsp_sync_zk_nodes(const Request* req) {
    LOG_TRACE("handle CMD_RSP_SYNC_ZK_NODES. fd: % d", req->fd());
    int ret = check_rsp(req);
    if (ret != ERR_OK) {
        LOG_ERROR("CMD_RSP_SYNC_ZK_NODES is not ok! fd: %d", req->fd());
        return ret;
    }

    kim::zk_node* zn;
    kim::register_node rn;

    if (!rn.ParseFromString(req->msg_body()->data())) {
        LOG_ERROR("parse CMD_RSP_SYNC_ZK_NODES data failed! fd: %d", req->fd());
        return ERR_INVALID_RESPONSE;
    }

    if (rn.version() != m_net->nodes()->version()) {
        m_net->nodes()->clear();
        m_net->nodes()->set_my_zk_node_path(rn.my_zk_path());
        for (int i = 0; i < rn.nodes_size(); i++) {
            zn = rn.mutable_nodes(i);
            m_net->nodes()->add_zk_node(*zn);
        }
        m_net->nodes()->print_debug_nodes_info();
    }

    return ERR_OK;
}

int SysCmd::check_rsp(const Request* req) {
    if (!req->msg_body()->has_rsp_result()) {
        LOG_ERROR("no rsp result! fd: %d, cmd: %d", req->fd(), req->msg_head()->cmd());
        return ERR_INVALID_RESPONSE;
    }

    if (req->msg_body()->rsp_result().code() != ERR_OK) {
        LOG_ERROR("rsp code is not ok, error! fd: %d, error: %d, errstr: %s",
                  req->fd(), req->msg_body()->rsp_result().code(),
                  req->msg_body()->rsp_result().msg().c_str());
    }

    return ERR_OK;
}

void SysCmd::on_repeat_timer() {
    // if (m_net->is_worker()) {
    //     /* every 5 minutes. */
    //     if (++m_timer_index % (5 * 10 * 60) == 2) {
    //         /* keep data consistent between manager and worker. */
    //         send_zk_nodes_version_to_manager(m_net->nodes()->version());
    //     }
    // }
}

int SysCmd::on_req_add_zk_node(const Request* req) {
    LOG_TRACE("handle CMD_REQ_ADD_ZK_NODE. fd: % d", req->fd());

    int ret;
    zk_node zn;

    if (!zn.ParseFromString(req->msg_body()->data())) {
        LOG_ERROR("parse tell worker node info failed! fd: %d", req->fd());
        m_net->send_ack(req, ERR_INVALID_MSG_DATA, "parse request failed!");
        return ERR_INVALID_PROTOBUF_PACKET;
    }

    if (!m_net->nodes()->add_zk_node(zn)) {
        LOG_ERROR("add zk node failed! fd: %d, path: %s", zn.path().c_str(), req->fd());
        ret = m_net->send_ack(req, ERR_FAILED, "add zk node failed!");
        if (ret != ERR_OK) {
            LOG_ERROR("send CMD_RSP_ADD_ZK_NODE failed! fd: %d", req->fd());
            return ERR_SEND_DATA_FAILED;
        }
        return ERR_OK;
    }

    m_net->nodes()->print_debug_nodes_info();

    ret = m_net->send_ack(req, ERR_OK, "ok");
    if (ret != ERR_OK) {
        LOG_ERROR("send CMD_RSP_ADD_ZK_NODE failed! fd: %d", req->fd());
        return ret;
    }

    return ERR_OK;
}

int SysCmd::on_rsp_add_zk_node(const Request* req) {
    int ret = check_rsp(req);
    if (ret != ERR_OK) {
        LOG_ERROR("CMD_RSP_ADD_ZK_NODE is not ok! fd: %d", req->fd());
        return ret;
    }
    return ERR_OK;
}

int SysCmd::on_req_del_zk_node(const Request* req) {
    LOG_TRACE("handle CMD_REQ_DEL_ZK_NODE. fd: % d", req->fd());

    int ret;
    std::string zk_path;

    zk_path = req->msg_body()->data();
    if (zk_path.empty()) {
        LOG_ERROR("parse tell worker node info failed! fd: %d", req->fd());
        m_net->send_ack(req, ERR_INVALID_MSG_DATA, "parse request failed!");
        return ERR_INVALID_RESPONSE;
    }

    m_net->nodes()->del_zk_node(zk_path);
    m_net->nodes()->print_debug_nodes_info();

    ret = m_net->send_ack(req, ERR_OK, "ok");
    if (ret != ERR_OK) {
        LOG_ERROR("send CMD_RSP_DEL_ZK_NODE failed! fd: %d", req->fd());
        return ERR_SEND_DATA_FAILED;
    }

    return ERR_OK;
}

int SysCmd::on_rsp_del_zk_node(const Request* req) {
    int ret = check_rsp(req);
    if (ret != ERR_OK) {
        LOG_ERROR("CMD_RSP_DEL_ZK_NODE is not ok! fd: %d", req->fd());
        return ret;
    }
    return ERR_OK;
}

int SysCmd::on_req_reg_zk_node(const Request* req) {
    LOG_TRACE("handle CMD_REQ_REGISTER_NODE. fd: % d", req->fd());

    int ret;
    kim::zk_node* zn;
    kim::register_node rn;

    if (!rn.ParseFromString(req->msg_body()->data())) {
        LOG_ERROR("parse CMD_REQ_REGISTER_NODE data failed! fd: %d", req->fd());
        m_net->send_ack(req, ERR_INVALID_MSG_DATA, "parse request data failed!");
        return ERR_INVALID_RESPONSE;
    }

    m_net->nodes()->clear();
    m_net->nodes()->set_my_zk_node_path(rn.my_zk_path());
    for (int i = 0; i < rn.nodes_size(); i++) {
        zn = rn.mutable_nodes(i);
        m_net->nodes()->add_zk_node(*zn);
    }
    m_net->nodes()->print_debug_nodes_info();

    ret = m_net->send_ack(req, ERR_OK, "ok");
    if (ret != ERR_OK) {
        LOG_ERROR("send CMD_RSP_REGISTER_NODE failed! fd: %d", req->fd());
        return ERR_SEND_DATA_FAILED;
    }

    return ERR_OK;
}

int SysCmd::on_rsp_reg_zk_node(const Request* req) {
    LOG_TRACE("handle CMD_RSP_REGISTER_NODE. fd: % d", req->fd());
    int ret = check_rsp(req);
    if (ret != ERR_OK) {
        LOG_ERROR("CMD_RSP_REGISTER_NODE is not ok! fd: %d", req->fd());
        return ret;
    }
    return ERR_OK;
}

/* manager. */
int SysCmd::on_req_sync_zk_nodes(const Request* req) {
    LOG_TRACE("handle CMD_REQ_SYNC_ZK_NODES. fd: % d", req->fd());

    /* check node version. sync reg nodes.*/
    uint32_t version;
    register_node rn;

    version = str_to_int(req->msg_body()->data());
    rn.set_version(version);

    if (m_net->nodes()->version() != version) {
        rn.set_my_zk_path(m_net->nodes()->get_my_zk_node_path());
        const std::unordered_map<std::string, zk_node>& nodes =
            m_net->nodes()->get_zk_nodes();
        for (const auto& it : nodes) {
            *rn.add_nodes() = it.second;
        }
    }

    LOG_TRACE("version, old: %d, new: %d", version, m_net->nodes()->version());

    int ret = m_net->send_ack(req, ERR_OK, "ok", rn.SerializeAsString());
    if (ret != ERR_OK) {
        LOG_ERROR("send CMD_REQ_SYNC_ZK_NODES failed! fd: %d", req->fd());
        return ERR_SEND_DATA_FAILED;
    }

    return ERR_OK;
}

int SysCmd::on_req_connect_to_worker(const Request* req) {
    /* B0. */
    LOG_DEBUG("A1 --> B0. B0 recv CMD_REQ_CONNECT_TO_WORKER, fd: %d", req->fd());

    channel_t ch;
    int fd, err, ret, channel, worker_index, worker_cnt;

    fd = req->fd();
    worker_cnt = m_net->worker_data_mgr()->get_infos().size();
    worker_index = str_to_int(req->msg_body()->data());

    if (worker_index == 0 || worker_index > worker_cnt) {
        m_net->send_ack(req, ERR_INVALID_WORKER_INDEX, "invalid worker index!");
        LOG_ERROR("invalid worker index, fd: %d, worker index: %d, worker cnt: %d",
                  fd, worker_index, worker_cnt);
        return ERR_INVALID_WORKER_INDEX;
    }

    LOG_DEBUG("B0 --> A1. CMD_RSP_CONNECT_TO_WORKER, fd: %d", req->fd());

    ret = m_net->send_ack(req, ERR_OK, "ok");
    if (ret != ERR_OK) {
        LOG_ERROR("send CMD_RSP_CONNECT_TO_WORKER failed! fd: %d", fd);
        return ERR_SEND_DATA_FAILED;
    }

    /* manager transfer fd to worker. */
    channel = m_net->worker_data_mgr()->get_worker_data_fd(worker_index);
    ch = {fd, AF_INET, static_cast<int>(Codec::TYPE::PROTOBUF), 1};
    for (;;) {
        err = write_channel(channel, &ch, sizeof(channel_t), m_logger);
        if (err == ERR_OK) {
            LOG_TRACE("write channel done! transfer fd: %d", fd);
            return ERR_TRANSFER_FD_DONE;
        } else if (err == EAGAIN) {
            LOG_WARN("wait to write again, fd: %d, errno: %d", fd, err);
            struct pollfd pf = {0};
            pf.fd = channel;
            pf.events = POLLOUT | POLLERR | POLLHUP;
            poll(&pf, 1, 1000);
            continue;
        } else {
            LOG_ERROR("transfer fd failed! ch fd: %d, fd: %d, errno: %d", channel, fd, err);
            return ERR_TRANSFER_FD_FAILED;
        }
    }
}

int SysCmd::on_req_tell_worker(const Request* req) {
    /* B1 */
    LOG_TRACE("B1 --> A1 CMD_REQ_TELL_WORKER. fd: %d", req->fd());

    int ret;
    target_node tn;
    std::string node_id;

    if (!tn.ParseFromString(req->msg_body()->data())) {
        LOG_ERROR("parse tell worker node info failed! fd: %d", req->fd());
        m_net->send_ack(req, ERR_INVALID_MSG_DATA, "parse request failed!");
        return ERR_INVALID_RESPONSE;
    }

    /* B1 send ack to A1. */
    tn.Clear();
    tn.set_node_type(m_net->node_type());
    tn.set_ip(m_net->node_host());
    tn.set_port(m_net->node_port());
    tn.set_worker_index(m_net->worker_index());

    ret = m_net->send_ack(req, ERR_OK, "ok", tn.SerializeAsString());
    if (ret != ERR_OK) {
        LOG_ERROR("send CMD_RSP_TELL_WORKER failed! fd: %d", req->fd());
        return ERR_REDIS_CONNECT_FAILED;
    }

    /* B1 connect A1 ok. */
    node_id = format_nodes_id(tn.ip(), tn.port(), tn.worker_index());
    m_net->update_conn_state(req->fd(), Connection::STATE::CONNECTED);
    m_net->add_client_conn(node_id, req->fd_data());
    return ERR_OK;
}

int SysCmd::on_rsp_tell_worker(const Request* req) {
    /* A1 */
    LOG_TRACE("A1 receives B1's CMD_RSP_TELL_WORKER. fd: %d", req->fd());

    int ret;
    target_node tn;
    std::string node_id;

    ret = check_rsp(req);
    if (ret != ERR_OK) {
        LOG_ERROR("CMD_RSP_TELL_WORKER is not ok! fd: %d", req->fd());
        return ret;
    }

    /* A1 save B1 worker info. */
    if (!tn.ParseFromString(req->msg_body()->data())) {
        LOG_ERROR("CMD_RSP_TELL_WORKER, parse B1' result failed! fd: %d", req->fd());
        return ERR_INVALID_RESPONSE;
    }

    node_id = format_nodes_id(tn.ip(), tn.port(), tn.worker_index());
    m_net->update_conn_state(req->fd(), Connection::STATE::CONNECTED);
    m_net->add_client_conn(node_id, req->fd_data());

    /* A1 begin to send waiting buffer. */
    return ERR_OK;
}

int SysCmd::on_rsp_connect_to_worker(const Request* req) {
    /* A1 receives rsp from B0. */
    LOG_TRACE("A1 receive B0's CMD_RSP_CONNECT_TO_WORKER. fd: %d", req->fd());

    int ret;
    target_node tn;

    ret = check_rsp(req);
    if (ret != ERR_OK) {
        LOG_ERROR("CMD_RSP_CONNECT_TO_WORKER is not ok! fd: %d", req->fd());
        return ret;
    }

    /* A1 --> B1: CMD_REQ_TELL_WORKER */
    tn.set_node_type(m_net->node_type());
    tn.set_ip(m_net->node_host());
    tn.set_port(m_net->node_port());
    tn.set_worker_index(m_net->worker_index());

    LOG_TRACE("A1 --> B1: CMD_REQ_TELL_WORKER. fd: %d", req->fd());

    ret = m_net->send_req(req->fd_data(), CMD_REQ_TELL_WORKER, m_net->new_seq(), tn.SerializeAsString());
    if (ret != ERR_OK) {
        LOG_ERROR("send data failed! fd: %d, ip: %s, port: %d, worker_index: %d",
                  req->fd(), tn.ip().c_str(), tn.port(), tn.worker_index());
        return ret;
    }

    return ERR_OK;
}

}  // namespace kim