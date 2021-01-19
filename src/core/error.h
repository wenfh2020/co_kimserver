#ifndef __KIM_ERROR_H__
#define __KIM_ERROR_H__

namespace kim {

enum E_ERROR {
    // common.
    ERR_OK = 0,
    ERR_FAILED = 1,
    ERR_INVALID_MSG_DATA = 2,
    ERR_INVALID_WORKER_INDEX = 3,
    ERR_UNKOWN_CMD = 4,
    ERR_INVALID_PARAMS = 5,

    ERR_EXEC_SETP = 101,
    ERR_EXEC_CMD_TIMEUOT = 102,

    // redis.
    ERR_REDIS_CONNECT_FAILED = 11001,
    ERR_REDIS_DISCONNECT = 11002,
    ERR_REDIS_CALLBACK = 11003,

    // database.
    ERR_DB_FAILED = 12001,
    ERR_DB_GET_CONNECTION = 12002,
    ERR_DB_INVALID_QUERY_SQL = 12003,
    ERR_DB_QUERY_FAILED = 12004, /* read. */
    ERR_DB_EXEC_FAILED = 12005,  /* write. */
    ERR_DB_CAT_NOT_FIND_NODE = 12006,
};

}  // namespace kim

#endif  //__KIM_ERROR_H__