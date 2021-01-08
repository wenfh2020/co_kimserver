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

    ERR_EXEC_SETP = 101,
    ERR_EXEC_CMD_TIMEUOT = 102,

    // redis.
    ERR_REDIS_CONNECT_FAILED = 11001,
    ERR_REDIS_DISCONNECT = 11002,
    ERR_REDIS_CALLBACK = 11003,

    // database
    ERR_DATABASE_FAILED = 12001,
};

}  // namespace kim

#endif  //__KIM_ERROR_H__