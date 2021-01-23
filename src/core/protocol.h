#ifndef __KIM_PROTOCOL_H__
#define __KIM_PROTOCOL_H__

/* system cmd. */

enum E_CMD {
    CMD_UNKNOWN = 0,

    /* communication between nodes.  */
    CMD_REQ_CONNECT_TO_WORKER = 21,
    CMD_RSP_CONNECT_TO_WORKER = 22,
    CMD_REQ_TELL_WORKER = 23,
    CMD_RSP_TELL_WORKER = 24,

    /* Parent and children process communicate. */

    /* zookeeper notice. */
    CMD_REQ_ADD_ZK_NODE = 41,
    CMD_RSP_ADD_ZK_NODE = 42,
    CMD_REQ_DEL_ZK_NODE = 43,
    CMD_RSP_DEL_ZK_NODE = 44,
    CMD_REQ_SYNC_ZK_NODES = 45,
    CMD_RSP_SYNC_ZK_NODES = 46,
    CMD_REQ_REGISTER_NODE = 47,
    CMD_RSP_REGISTER_NODE = 48,
    CMD_REQ_UPDATE_PAYLOAD = 49,
    CMD_RSP_UPDATE_PAYLOAD = 50,
};

#endif  //__KIM_PROTOCOL_H__