#ifndef __KIM_NETWORK_H__
#define __KIM_NETWORK_H__

#include "codec/codec.h"
#include "connection.h"
#include "coroutines.h"
#include "module_mgr.h"
#include "net.h"
#include "net/anet.h"
#include "net/chanel.h"
#include "nodes.h"
#include "protobuf/sys/nodes.pb.h"
#include "protobuf/sys/payload.pb.h"
#include "util/log.h"
#include "util/util.h"
#include "worker_data_mgr.h"

namespace kim {

class Network : public INet {
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
    /* for worker.  */
    bool create_w(const CJsonObject& config, int ctrl_fd, int data_fd, int index);
    void destory();

    virtual uint64_t now() override { return mstime(); }
    virtual uint64_t new_seq() override { return ++m_seq; }

    /* events. */
    void run();

    bool set_gate_codec(const std::string& codec);
    void set_keep_alive(uint64_t ms) { m_keep_alive = ms; }
    uint64_t keep_alive() { return m_keep_alive; }
    bool is_request(int cmd) { return (cmd & 0x00000001); }

    bool load_config(const CJsonObject& config);
    bool load_public(const CJsonObject& config);
    bool load_worker_data_mgr();
    bool load_modules();
    bool load_corotines();
    WorkerDataMgr* worker_data_mgr() { return m_worker_data_mgr; }

    /* use in fork. */
    void close_fds();
    /* close connection by fd. */
    bool close_conn(int fd);
    Connection* create_conn(int fd, Codec::TYPE codec, bool is_chanel = false);
    /* manager and worker contack by socketpair. */
    void close_chanel(int* fds);

    Connection* get_conn(const fd_t& f);
    void clear_routines();

    virtual int send_to(Connection* c, const MsgHead& head, const MsgBody& body) override;
    virtual int send_to(const fd_t& f, const MsgHead& head, const MsgBody& body) override;
    virtual int send_ack(const Request* req, int err, const std::string& errstr = "", const std::string& data = "") override;

   private:
    /* socket. */
    int listen_to_port(const char* host, int port);
    void close_fd(int fd);

    bool close_conn(Connection* c);
    Connection* create_conn(int fd);

    bool process_msg(Connection* c);
    bool process_tcp_msg(Connection* c);
    bool process_http_msg(Connection* c);

    /* coroutines. */
    static void* co_handler_accept_nodes_conn(void*);
    static void* co_handler_accept_gate_conn(void*);
    void* handler_accept_gate_conn(void*);
    static void* co_handler_read_transfer_fd(void*);
    void* handler_read_transfer_fd(void*);
    static void* co_handler_requests(void*);
    void* handler_requests(void*);

   private:
    Log* m_logger;                                             /* logger. */
    Codec::TYPE m_gate_codec = Codec::TYPE::UNKNOWN;           /* gate codec type. */
    std::unordered_map<int, Connection*> m_conns;              /* key: fd, value: connection. */
    std::unordered_map<std::string, Connection*> m_node_conns; /* key: node_id */

    uint64_t m_seq = 0;          /* cur increasing sequence. */
    char m_errstr[ANET_ERR_LEN]; /* error string. */

    TYPE m_type = TYPE::UNKNOWN;                      /* owner type. */
    uint64_t m_keep_alive = IO_TIMEOUT_VAL;           /* io timeout time. */
    WorkerDataMgr* m_worker_data_mgr = nullptr;       /* manager handle worker data. */
    std::list<chanel_resend_data_t*> m_wait_send_fds; /* sendmsg maybe return -1 and errno == EAGAIN. */

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

    Nodes* m_nodes = nullptr; /* server nodes. ketama nodes manager. */

    Coroutines* m_coroutines = nullptr;
    ModuleMgr* m_module_mgr = nullptr; /* modules so. */
    Payload m_payload;                 /* payload data. */
};

}  // namespace kim

#endif  // __KIM_NETWORK_H__