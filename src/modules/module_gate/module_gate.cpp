#include "module_gate.h"

MUDULE_CREATE(MoudleGate)

namespace kim {

int MoudleGate::filter_request(const Request* req) {
    int ret;
    MsgHead* head_out = new MsgHead;
    MsgBody* body_out = new MsgBody;
    MsgHead* head_in = (MsgHead*)req->msg_head();
    MsgBody* body_in = (MsgBody*)req->msg_body();

    /* send to other nodes ("logic"). */
    ret = net()->relay_to_node(
        "logic", req->msg_body()->data(), head_in, body_in, head_out, body_out);

    if (ret != ERR_OK) {
        ret = net()->send_ack(req, ret, "relay to node failed!");
    } else {
        ret = net()->send_to(req->fd_data(), *head_out, *body_out);
    }

    LOG_DEBUG("req, cmd: %d, seq: %u, len: %d, body data: <%s>",
              req->msg_head()->cmd(), req->msg_head()->seq(),
              req->msg_head()->len(), req->msg_body()->data().c_str());

    LOG_DEBUG("ack, cmd: %d, seq: %u, body len: %d, error: %d, errstr: %s, data: %s",
              head_out->cmd(), head_out->seq(), head_out->len(),
              body_out->mutable_rsp_result()->code(),
              body_out->mutable_rsp_result()->msg().c_str(),
              body_out->data().c_str());

    SAFE_DELETE(head_out);
    SAFE_DELETE(body_out);
    return ret;
}

}  // namespace kim