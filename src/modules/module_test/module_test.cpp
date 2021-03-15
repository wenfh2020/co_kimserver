#include "module_test.h"

#include "redis/redis_mgr.h"

MUDULE_CREATE(MoudleTest)

namespace kim {

int MoudleTest::test_hello(const Request* req) {
    LOG_DEBUG("cmd: %d, seq: %u, len: %d, body data: <%s>",
              req->msg_head()->cmd(), req->msg_head()->seq(),
              req->msg_head()->len(), req->msg_body()->data().c_str());
    return net()->send_ack(req, ERR_OK, "ok", "good job!");
}

int MoudleTest::test_mysql(const Request* req) {
    LOG_DEBUG("cmd: %d, seq: %u, len: %d, body data: <%s>",
              req->msg_head()->cmd(), req->msg_head()->seq(),
              req->msg_head()->len(), req->msg_body()->data().c_str());

    int ret;
    int id = 1;
    char cmd[1024];
    vec_row_t* rows;
    const char* node = "test";

    /* write mysql. */
    snprintf(cmd, sizeof(cmd),
             "update mytest.test_async_mysql set value = '%s' where id = %d;",
             req->msg_body()->data().c_str(), id);
    ret = net()->mysql_mgr()->sql_write(node, cmd);
    if (ret != ERR_OK) {
        LOG_ERROR("write mysql failed! ret: %d", ret);
        return net()->send_ack(req, ret, "write mysql failed!");
    }

    rows = new vec_row_t;

    /* read mysql. */
    snprintf(cmd, sizeof(cmd), "select value from mytest.test_async_mysql where id = %d;", id);
    ret = net()->mysql_mgr()->sql_read(node, cmd, *rows);
    if (ret != ERR_OK) {
        SAFE_DELETE(rows);
        LOG_ERROR("read mysql failed! ret: %d", ret);
        return net()->send_ack(req, ret, "read mysql failed!");
    }

    for (size_t i = 0; i < rows->size(); i++) {
        const map_row_t& items = rows->at(i);
        for (const auto& it : items) {
            LOG_DEBUG("col: %s, data: %s", it.first.c_str(), it.second.c_str());
        }
    }

    SAFE_DELETE(rows);

    return net()->send_ack(req, ERR_OK, "ok", "test mysql done!");
}

int MoudleTest::test_redis(const Request* req) {
    LOG_DEBUG("cmd: %d, seq: %u, len: %d, body data: <%s>",
              req->msg_head()->cmd(), req->msg_head()->seq(),
              req->msg_head()->len(), req->msg_body()->data().c_str());

    int ret;
    int id = 1;
    char cmd[1024];
    const char* node = "test";
    const char* key = "key";
    const char* val = "value";
    redisReply* reply = nullptr;

    /* write redis. */
    snprintf(cmd, sizeof(cmd), "set %s %s:%d", key, val, id);
    ret = net()->redis_mgr()->exec_cmd(node, cmd, &reply);
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

}  // namespace kim