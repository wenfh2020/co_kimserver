#ifndef __KIM_ZK_TASK_H__
#define __KIM_ZK_TASK_H__

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

}  // namespace kim

#endif  //__KIM_ZK_TASK_H__