#include "module_gate.h"

MUDULE_CREATE(MoudleGate)

namespace kim {

int MoudleGate::filter_request(std::shared_ptr<Msg> req) {
    auto ack = std::make_shared<Msg>();
    auto ret = net()->relay_to_node("logic", req->body()->data(), req, ack);
    if (ret != ERR_OK) {
        ret = net()->send_ack(req, ret, "relay to node failed!");
    } else {
        ret = net()->send_to(req->ft(), ack);
    }

    LOG_DEBUG("ack, cmd: %d, seq: %u, body len: %d, error: %d, errstr: %s, data: %s",
              ack->head()->cmd(), ack->head()->seq(), ack->head()->len(),
              ack->body()->mutable_rsp_result()->code(),
              ack->body()->mutable_rsp_result()->msg().c_str(),
              ack->body()->data().c_str());

    LOG_DEBUG("req, cmd: %d, seq: %u, len: %d, body data: <%s>",
              req->head()->cmd(), req->head()->seq(),
              req->head()->len(), req->body()->data().c_str());

    return ret;
}

}  // namespace kim