#pragma once

#include "module.h"
#include "protocol.h"

namespace kim {

class MoudleTest : public Module {
    REGISTER_HANDLER(MoudleTest)

   public:
    void register_handle_func() {
        HANDLE_PROTO_FUNC(KP_REQ_TEST_HELLO, MoudleTest::on_test_hello);
        HANDLE_PROTO_FUNC(KP_REQ_TEST_MYSQL, MoudleTest::on_test_mysql);
        HANDLE_PROTO_FUNC(KP_REQ_TEST_REDIS, MoudleTest::on_test_redis);
        HANDLE_PROTO_FUNC(KP_REQ_TEST_SESSION, MoudleTest::on_test_session);
    }

   protected:
    void print_cmd_info(std::shared_ptr<Request> req);

   private:
    /* request's handler. */
    int on_test_hello(std::shared_ptr<Request> req);
    int on_test_mysql(std::shared_ptr<Request> req);
    int on_test_redis(std::shared_ptr<Request> req);
    int on_test_session(std::shared_ptr<Request> req);
};

}  // namespace kim