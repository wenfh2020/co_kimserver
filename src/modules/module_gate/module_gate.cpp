#include "module_gate.h"

MUDULE_CREATE(MoudleGate)

namespace kim {

int MoudleGate::filter_request(const Request* req) {
    LOG_DEBUG("cmd: %d, seq: %u, len: %d, body data: <%s>",
              req->msg_head()->cmd(), req->msg_head()->seq(),
              req->msg_head()->len(), req->msg_body()->data().c_str());

    int ret;
    MsgHead head_out;
    MsgBody body_out;
    /* send to other nodes. */

    ret = net()->relay_to_node(
        "logic", req->msg_body()->data(),
        (MsgHead*)req->msg_head(), (MsgBody*)req->msg_body(), &head_out, &body_out);

    if (ret != ERR_OK) {
        return net()->send_ack(req, ret, "send to node failed!");
    } else {
        return net()->send_ack(req, ERR_OK, "ok", body_out.data());
    }
}

}  // namespace kim