#ifndef __MODULE_TEST_H__
#define __MODULE_TEST_H__

#include "module.h"
#include "protocol.h"

namespace kim {

class MoudleTest : public Module {
    REGISTER_HANDLER(MoudleTest)

   public:
    void register_handle_func() {
        HANDLE_PROTO_FUNC(KP_REQ_TEST_HELLO, MoudleTest::test_hello);
    }

   private:
    // protobuf.
    int test_hello(const Request* req);
};

}  // namespace kim

#endif  //__MODULE_TEST_H__