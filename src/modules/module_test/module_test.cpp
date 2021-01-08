#include "module_test.h"

MUDULE_CREATE(MoudleTest)

namespace kim {

int MoudleTest::test_hello(const fd_t& fdata, const MsgHead& head, const MsgBody& body) {
    LOG_DEBUG("cmd: %d, seq: %u, len: %d",
              head.cmd(), head.seq(), head.len());

    LOG_DEBUG("body data: <%s>", body.SerializeAsString().c_str());

    // return net()->send_ack(req, ERR_OK, "ok", "good job!")
    //            ? Cmd::STATUS::OK
    //            : Cmd::STATUS::ERROR;
    return 0;
}

}  // namespace kim