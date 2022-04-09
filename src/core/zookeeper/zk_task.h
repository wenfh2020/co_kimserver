#pragma once

#include <iostream>
#include <vector>

namespace kim {

typedef struct zk_task_s zk_task_t;

/* zk task result. */
struct zk_result {
    int error = 0;                   /* error number. */
    std::string errstr;              /* error string. */
    std::string value;               /* result value. */
    std::vector<std::string> values; /* result values. */
};

typedef struct zk_task_s {
    /* task command. */
    enum class CMD {
        UNKNOWN = 0,
        REGISTER,                  /* node register. */
        GET,                       /* get node info. */
        SET_DATA,                  /* set node data. */
        DELETE,                    /* delete node. */
        NOTIFY_SESSION_CONNECTD,   /* notify node connected status. */
        NOTIFY_SESSION_CONNECTING, /* notify node connecting status. */
        NOTIFY_SESSION_EXPIRED,    /* notify node expried. */
        NOTIFY_NODE_CREATED,       /* notify node created which had been watched by cur node. */
        NOTIFY_NODE_DELETED,       /* notify node deleted which had been watched by cur node. */
        NOTIFY_NODE_DATA_CAHNGED,  /* notify node data changed which had been watched by cur node. */
        NOTIFY_NODE_CHILD_CAHNGED, /* notify node children change which had been watched by cur node. */
        END,
    };
    std::string path;   /* node path. */
    std::string value;  /* node value. */
    CMD cmd;            /* node command. */
    double create_time; /* task create time. */
    zk_result res;      /* zookeeper's result. */
} zk_task_t;

static const char* zk_cmd_to_string(zk_task_t::CMD cmd) {
    if (cmd == zk_task_t::CMD::UNKNOWN) {
        return "unknwon";
    } else if (cmd == zk_task_t::CMD::REGISTER) {
        return "register";
    } else if (cmd == zk_task_t::CMD::GET) {
        return "get";
    } else if (cmd == zk_task_t::CMD::SET_DATA) {
        return "set payload data";
    } else if (cmd == zk_task_t::CMD::DELETE) {
        return "delete node";
    } else if (cmd == zk_task_t::CMD::NOTIFY_SESSION_CONNECTD) {
        return "session connected";
    } else if (cmd == zk_task_t::CMD::NOTIFY_SESSION_CONNECTING) {
        return "session connecting";
    } else if (cmd == zk_task_t::CMD::NOTIFY_SESSION_EXPIRED) {
        return "session expired";
    } else if (cmd == zk_task_t::CMD::NOTIFY_NODE_CREATED) {
        return "node created";
    } else if (cmd == zk_task_t::CMD::NOTIFY_NODE_DELETED) {
        return "node deleted";
    } else if (cmd == zk_task_t::CMD::NOTIFY_NODE_DATA_CAHNGED) {
        return "data change";
    } else if (cmd == zk_task_t::CMD::NOTIFY_NODE_CHILD_CAHNGED) {
        return "child change";
    } else {
        return "invalid cmd";
    }
}

}  // namespace kim
