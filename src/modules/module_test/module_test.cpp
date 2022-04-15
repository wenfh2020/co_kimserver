#include "module_test.h"

#include "redis/redis_mgr.h"
#include "user_session.h"

MUDULE_CREATE(MoudleTest)

namespace kim {

void MoudleTest::print_cmd_info(std::shared_ptr<Request> req) {
    LOG_DEBUG("cmd: %d, seq: %u, len: %d, body data: <%s>",
              req->msg_head()->cmd(), req->msg_head()->seq(),
              req->msg_head()->len(), req->msg_body()->data().c_str());
}

int MoudleTest::on_test_hello(std::shared_ptr<Request> req) {
    print_cmd_info(req);
    return net()->send_ack(req, ERR_OK, "ok", "good job!");
}

int MoudleTest::on_test_mysql(std::shared_ptr<Request> req) {
    print_cmd_info(req);

    const char* read_sql = "select value from mytest.test_async_mysql where id = 1;";
    const char* write_sql = "insert into mytest.test_async_mysql (value) values ('hello world');";

    auto ret = net()->mysql_mgr()->sql_write("test", write_sql);
    if (ret != ERR_OK) {
        LOG_ERROR("write mysql failed! ret: %d", ret);
        return net()->send_ack(req, ret, "write mysql failed!");
    }

    auto rows = std::make_shared<VecMapRow>();
    auto ret = net()->mysql_mgr()->sql_read("test", read_sql, rows);
    if (ret != ERR_OK) {
        LOG_ERROR("read mysql failed! ret: %d", ret);
        return net()->send_ack(req, ret, "read mysql failed!");
    }

    for (size_t i = 0; i < rows->size(); i++) {
        const auto& cols = rows->at(i);
        for (const auto& it : cols) {
            LOG_DEBUG("col: %s, data: %s", it.first.c_str(), it.second.c_str());
        }
    }

    return net()->send_ack(req, ERR_OK, "ok", "test mysql done!");
}

int MoudleTest::on_test_redis(std::shared_ptr<Request> req) {
    print_cmd_info(req);

    int id = 1;
    char cmd[1024];
    const char* node = "test";
    const char* key = "key";
    const char* val = "value";
    redisReply* reply = nullptr;

    /* write redis. */
    snprintf(cmd, sizeof(cmd), "set %s %s:%d", key, val, id);
    auto ret = net()->redis_mgr()->exec_cmd(node, cmd, &reply);
    if (ret != REDIS_OK || reply == nullptr) {
        ret = ERR_REDIS_WRITE_FAILED;
        LOG_ERROR("write redis failed!");
        return net()->send_ack(req, ret, "write redis failed!");
    }
    freeReplyObject(reply);

    LOG_DEBUG("write redis done! cmd: %s", cmd);

    /* read redis. */
    snprintf(cmd, sizeof(cmd), "get %s", key);
    ret = net()->redis_mgr()->exec_cmd(node, cmd, &reply);
    if (ret != REDIS_OK || reply == nullptr) {
        LOG_ERROR("read redis failed!");
        ret = ERR_REDIS_READ_FAILED;
        return net()->send_ack(req, ret, "read redis failed!");
    }
    freeReplyObject(reply);

    LOG_DEBUG("read redis done! cmd: %s", cmd);
    return net()->send_ack(req, ERR_OK, "ok", "test redis done!");
}

int MoudleTest::on_test_session(std::shared_ptr<Request> req) {
    print_cmd_info(req);

    CJsonObject json_data;
    if (!json_data.Parse(req->msg_body()->data())) {
        LOG_ERROR("invalid json data!");
        return net()->send_ack(req, ERR_FAILED, "fail", "invalid json data!");
    }
    LOG_DEBUG("user_id: %s, user_name: %s",
              json_data("user_id").c_str(), json_data("user_name").c_str());

    auto sessid = json_data("user_id");
    auto session = SESS_MGR->get_alloc_session<UserSession>(sessid);
    if (session == nullptr) {
        LOG_ERROR("get alloc session failed! sessid: %s", sessid.c_str());
        return net()->send_ack(req, ERR_FAILED, "fail", "get alloc session failed!");
    }

    session->set_user_id(str_to_int(sessid));
    session->set_user_name(json_data("user_name"));

    if (!SESS_MGR->del_session(sessid)) {
        LOG_ERROR("delete session failed! sessid: %s", sessid.c_str());
        return net()->send_ack(req, ERR_FAILED, "fail", "del session failed!");
    }

    return net()->send_ack(req, ERR_OK, "done", "test session!");
}

}  // namespace kim