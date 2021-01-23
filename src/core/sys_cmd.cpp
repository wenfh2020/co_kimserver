#include "sys_cmd.h"

#include "net/chanel.h"
#include "protocol.h"
#include "worker_data_mgr.h"

namespace kim {

SysCmd::SysCmd(Log* logger, INet* net)
    : Logger(logger), m_net(net) {
}

int SysCmd::check_rsp(const Request* req) {
    if (!req->msg_body()->has_rsp_result()) {
        LOG_ERROR("no rsp result! fd: %d, cmd: %d", req->fd(), req->msg_head()->cmd());
        return ERR_INVALID_RESPONSE;
    }

    if (req->msg_body()->rsp_result().code() != ERR_OK) {
        LOG_ERROR("rsp code is not ok, error! fd: %d, error: %d, errstr: %s",
                  req->fd(),
                  req->msg_body()->rsp_result().code(),
                  req->msg_body()->rsp_result().msg().c_str());
    }

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

int SysCmd::handle_msg(const Request* req) {
    if (req == nullptr) {
        return ERR_INVALID_PARAMS;
    }

    /* system cmd < 1000. */
    if (req->msg_head()->cmd() > 1000) {
        return ERR_UNKOWN_CMD;
    }

    return (m_net->is_manager()) ? handle_manager_msg(req) : handle_worker_msg(req);
}

int SysCmd::handle_manager_msg(const Request* req) {
    LOG_TRACE("process manager message.");
    switch (req->msg_head()->cmd()) {
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

}  // namespace kim