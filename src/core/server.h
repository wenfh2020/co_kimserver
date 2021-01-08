#ifndef __KIM_SERVER__
#define __KIM_SERVER__

#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fstream>
#include <iosfwd>
#include <iostream>
#include <list>
#include <set>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace kim {

// exit code.
#define EXIT_SUCCESS 0
#define EXIT_FAIL -1
#define EXIT_CHILD -2
#define EXIT_CHILD_INIT_FAIL -3
#define EXIT_FD_TRANSFER -4

// redis operation return status.
enum class E_RDS_STATUS {
    OK = 0,
    WAITING = 2,
    ERROR = 3,
};

typedef struct fd_s {
    int fd;
    uint64_t id;
} fd_t;

// time out info.
#define CMD_MAX_TIMEOUT_CNT 3      // default max cmd time out count.
#define SESSION_MAX_TIMEOUT_CNT 3  // default max session time out count.
#define IO_TIMEOUT_VAL 15.0        // connection time out value.
#define CMD_TIMEOUT_VAL 3.0        // cmd time out value.
#define REPEAT_TIMEOUT_VAL 1.0     // repeat time out value.

#define MAX_PATH 256
#define TCP_BACK_LOG 511
#define NET_IP_STR_LEN 46 /* INET6_ADDRSTRLEN is 46, but we need to be sure */
#define MAX_ACCEPTS_PER_CALL 1000

// logger macro.
#define LOG_FORMAT(level, args...)                                           \
    if (m_logger != nullptr) {                                               \
        m_logger->log_data(__FILE__, __LINE__, __FUNCTION__, level, ##args); \
    }

#define LOG_EMERG(args...) LOG_FORMAT((kim::Log::LL_EMERG), ##args)
#define LOG_ALERT(args...) LOG_FORMAT((kim::Log::LL_ALERT), ##args)
#define LOG_CRIT(args...) LOG_FORMAT((kim::Log::LL_CRIT), ##args)
#define LOG_ERROR(args...) LOG_FORMAT((kim::Log::LL_ERR), ##args)
#define LOG_WARN(args...) LOG_FORMAT((kim::Log::LL_WARNING), ##args)
#define LOG_NOTICE(args...) LOG_FORMAT((kim::Log::LL_NOTICE), ##args)
#define LOG_INFO(args...) LOG_FORMAT((kim::Log::LL_INFO), ##args)
#define LOG_DEBUG(args...) LOG_FORMAT((kim::Log::LL_DEBUG), ##args)
#define LOG_TRACE(args...) LOG_FORMAT((kim::Log::LL_TRACE), ##args)

#define MUDULE_CREATE(module_name)       \
    extern "C" {                         \
    kim::Module* create() {              \
        return (new kim::module_name()); \
    }                                    \
    }

#define SAFE_FREE(x)               \
    {                              \
        if (x != nullptr) free(x); \
        x = nullptr;               \
    }

#define SAFE_DELETE(x)              \
    {                               \
        if (x != nullptr) delete x; \
        x = nullptr;                \
    }

#define SAFE_ARRAY_DELETE(x)          \
    {                                 \
        if (x != nullptr) delete[] x; \
        x = nullptr;                  \
    }

#define CHECK_SET(v, def, d) \
    if ((v) == nullptr) {    \
        v = new def;         \
    }                        \
    *(v) = (d);

#define CHECK_NEW(v, def) (v == nullptr) ? (v = new def) : (v)

}  // namespace kim

#endif  //__KIM_SERVER__