#include "module_test.h"

MUDULE_CREATE(MoudleTest)

namespace kim {

int MoudleTest::test_hello(const Request* req) {
    LOG_DEBUG("cmd: %d, seq: %u, len: %d",
              req->msg_head()->cmd(), req->msg_head()->seq(),
              req->msg_head()->len());
    LOG_DEBUG("body data: <%s>", req->msg_body()->data().c_str());

    return net()->send_ack(req, ERR_OK, "ok", "good job!");
}

int MoudleTest::filter_request(const Request* req) {
    LOG_DEBUG("cmd: %d, seq: %u, len: %d",
              req->msg_head()->cmd(), req->msg_head()->seq(),
              req->msg_head()->len());
    LOG_DEBUG("body data: <%s>", req->msg_body()->data().c_str());

    /* send to other nodes. */
    int ret = net()->send_to_node(
        "logic", req->msg_body()->data(), *req->msg_head(), *req->msg_body());
    if (ret != ERR_OK) {
        return net()->send_ack(req, ERR_FAILED, "send to node failed!");
    }

    /* get result. */
    return net()->send_ack(req, ERR_OK, "ok", "good job!");

    /* send to node. */
    return ERR_UNKOWN_CMD;
}

}  // namespace kim