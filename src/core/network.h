#ifndef __KIM_NETWORK_H__
#define __KIM_NETWORK_H__

#include <unordered_map>

#include "codec/codec.h"
#include "connection.h"
#include "libco/co_routine.h"
#include "net/anet.h"
#include "net/chanel.h"
#include "protobuf/sys/nodes.pb.h"
#include "server.h"
#include "util/json/CJsonObject.hpp"
#include "util/log.h"
#include "util/util.h"

namespace kim {

class Network {
   public:
    enum class TYPE {
        UNKNOWN = 0,
        MANAGER,
        WORKER,
    };

    /* manager transfer fd to worker by sendmsg, 
     * when ret == -1 && errno == EAGIN, resend again. */
    typedef struct chanel_resend_data_s {
        channel_t ch;
        int count = 0;
    } chanel_resend_data_t;

    Network(Log* logger, TYPE type);
    virtual ~Network();

    Network(const Network&) = delete;
    Network& operator=(const Network&) = delete;

    /* for manager. */
    bool create_m(const addr_info* ainfo, const CJsonObject& config);
    /* for worker. */
    bool create_w(const CJsonObject& config, int ctrl_fd, int data_fd, int index);
    void destory();

    uint64_t new_seq() { return ++m_seq; }

    /* events. */
    void run();

    bool set_gate_codec(const std::string& codec);
    void set_keep_alive(double secs) { m_keep_alive = secs; }
    double keep_alive() { return m_keep_alive; }
    bool is_request(int cmd) { return (cmd & 0x00000001); }

   private:
    /* socket. */
    int listen_to_port(const char* host, int port);
    void close_fd(int fd);

    Connection* create_conn(int fd);
    // Connection* create_conn(int fd, Codec::TYPE codec, bool is_chanel);

    /* coroutines. */
    static void* co_handler_accept_nodes_conn(void*);
    static void* co_handler_accept_gate_conn(void*);
    void* handler_accept_gate_conn(void*);
    static void* co_handler_requests(void*);

   private:
    Log* m_logger;                                   /* logger. */
    Codec::TYPE m_gate_codec = Codec::TYPE::UNKNOWN; /* gate codec type. */
    std::unordered_map<int, Connection*> m_conns;    /* key: fd, value: connection. */

    uint64_t m_seq = 0;          /* cur increasing sequence. */
    char m_errstr[ANET_ERR_LEN]; /* error string. */

    TYPE m_type = TYPE::UNKNOWN;          /* owner type. */
    double m_keep_alive = IO_TIMEOUT_VAL; /* io timeout time. */

    std::string m_node_type;
    std::string m_node_host;
    std::string m_gate_host;
    int m_node_port = 0;
    int m_gate_port = 0;
    int m_worker_index = 0;

    int m_node_host_fd = -1;    /* host for inner servers. */
    int m_gate_host_fd = -1;    /* gate host fd for client. */
    int m_manager_ctrl_fd = -1; /* chanel fd use for worker. */
    int m_manager_data_fd = -1; /* chanel fd use for worker. */
};

}  // namespace kim

#endif  // __KIM_NETWORK_H__