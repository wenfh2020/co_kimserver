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

int MoudleTest::test_auto_send(const Request* req) {
    LOG_DEBUG("cmd: %d, seq: %u, len: %d",
              req->msg_head()->cmd(), req->msg_head()->seq(),
              req->msg_head()->len());
    LOG_DEBUG("body data: <%s>", req->msg_body()->data().c_str());

    return net()->send_ack(req, ERR_OK, "ok", "auto send ack!");
}

}  // namespace kim