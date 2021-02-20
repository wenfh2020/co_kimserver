#ifndef __KIM_NETWORK_H__
#define __KIM_NETWORK_H__

#include "codec/codec.h"
#include "connection.h"
#include "coroutines.h"
#include "module_mgr.h"
#include "net.h"
#include "net/anet.h"
#include "net/channel.h"
#include "node_connection.h"
#include "nodes.h"
#include "sys_cmd.h"
#include "util/json/CJsonObject.hpp"
#include "worker_data_mgr.h"
#include "zk_client.h"

namespace kim {

class Network : public INet {
   public:
    enum class TYPE {
        UNKNOWN = 0,
        MANAGER,
        WORKER,
    };

    Network(Log* logger, TYPE type);
    virtual ~Network();

    Network(const Network&) = delete;
    Network& operator=(const Network&) = delete;

    /* for manager. */
    bool create_m(const addr_info* ainfo, const CJsonObject& config);
    /* for worker.  */
    bool create_w(const CJsonObject& config, int ctrl_fd, int data_fd, int index);

    bool init_manager_channel(int ctrl_fd, int data_fd);

    /* events. */
    void run();

    void destory();

    bool set_gate_codec(const std::string& codec);
    void set_keep_alive(uint64_t ms) { m_keep_alive = ms; }
    uint64_t keep_alive() { return m_keep_alive; }
    bool is_request(int cmd) { return (cmd & 0x00000001); }

    virtual CJsonObject* config() override { return &m_config; }
    virtual uint64_t now() override { return mstime(); }
    virtual uint64_t new_seq() override { return ++m_seq; }

    /* pro's type. */
    virtual bool is_worker() override { return m_type == TYPE::WORKER; }
    virtual bool is_manager() override { return m_type == TYPE::MANAGER; }

    virtual std::string node_type() override { return m_node_type; }
    virtual std::string node_host() override { return m_node_host; }
    virtual int node_port() override { return m_node_port; }
    virtual int worker_index() override { return m_worker_index; }

    virtual Nodes* nodes() override { return m_nodes; }
    virtual SysCmd* sys_cmd() override { return m_sys_cmd; }
    virtual MysqlMgr* mysql_mgr() override { return m_mysql_mgr; }
    virtual RedisMgr* redis_mgr() override { return m_redis_mgr; }
    virtual WorkerDataMgr* worker_data_mgr() override { return m_worker_data_mgr; }
    virtual ZkClient* zk_client() override { return m_zk_cli; }

    virtual int send_to(Connection* c, const MsgHead& head, const MsgBody& body) override;
    virtual int send_to(const fd_t& f, const MsgHead& head, const MsgBody& body) override;
    virtual int send_ack(const Request* req, int err, const std::string& errstr = "", const std::string& data = "") override;
    virtual int send_req(const fd_t& f, uint32_t cmd, uint32_t seq, const std::string& data) override;
    virtual int send_req(Connection* c, uint32_t cmd, uint32_t seq, const std::string& data) override;

    // virtual int auto_send(const std::string& ip, int port, int worker_index, const MsgHead& head, const MsgBody& body) override;
    virtual int relay_to_node(const std::string& node_type, const std::string& obj, MsgHead* head, MsgBody* body, MsgHead* head_out, MsgBody* body_out) override;

    /* channel. */
    virtual int send_to_manager(int cmd, uint64_t seq, const std::string& data) override;
    virtual int send_to_worker(int cmd, uint64_t seq, const std::string& data) override;

    /* connection. */
    virtual bool update_conn_state(int fd, Connection::STATE state) override;
    virtual bool add_client_conn(const std::string& node_id, const fd_t& f) override;

    /* connection. */
    void close_fds(); /* use in fork. */

    virtual void close_fd(int fd) override;
    virtual bool close_conn(int fd) override;
    virtual bool close_conn(Connection* c) override;
    virtual Connection* create_conn(int fd) override;

    void close_channel(int* fds); /* close socketpair. */
    Connection* create_conn(int fd, Codec::TYPE codec, bool is_channel = false);
    Connection* get_conn(const fd_t& f);
    Connection* get_node_conn(const std::string& host, int port, int worker_index);

    void clear_routines();
    void on_repeat_timer(); /* call by parent, 10 times/s on Linux. */

    /* payload. */
    bool report_payload_to_manager();
    bool report_payload_to_zookeeper();

   private:
    bool load_config(const CJsonObject& config);
    bool load_public(const CJsonObject& config);
    bool load_worker_data_mgr();
    bool load_modules();
    bool load_corotines();
    bool load_zk_mgr(); /* zookeeper client. */
    bool load_nodes_conn();
    bool load_mysql_mgr();
    bool load_redis_mgr();

    /* socket & connection. */
    int listen_to_port(const char* host, int port);

    int process_msg(Connection* c);
    int process_tcp_msg(Connection* c);
    int process_http_msg(Connection* c);

    /* coroutines. */
    static void* co_handle_accept_nodes_conn(void*);
    void* handle_accept_nodes_conn(void*);
    static void* co_handle_accept_gate_conn(void*);
    void* handle_accept_gate_conn(void*);
    static void* co_handle_read_transfer_fd(void*);
    void* handle_read_transfer_fd(void*);
    static void* co_handle_requests(void*);
    void* handle_requests(void*);

   private:
    Log* m_logger;                                             /* logger. */
    CJsonObject m_config;                                      /* config. */
    Codec::TYPE m_gate_codec = Codec::TYPE::UNKNOWN;           /* gate codec type. */
    std::unordered_map<int, Connection*> m_conns;              /* key: fd, value: connection. */
    std::unordered_map<std::string, Connection*> m_node_conns; /* key: node_id, value: connection. */

    uint64_t m_seq = 0;          /* incremental serial number. */
    char m_errstr[ANET_ERR_LEN]; /* error string. */

    TYPE m_type = TYPE::UNKNOWN;                /* owner type. */
    uint64_t m_keep_alive = IO_TIMEOUT_VAL;     /* io timeout. */
    WorkerDataMgr* m_worker_data_mgr = nullptr; /* manager handle worker data. */

    /* node for inner servers. */
    std::string m_node_host;
    int m_node_port = 0;
    int m_node_host_fd = -1;

    /* gate for client. */
    std::string m_gate_host;
    int m_gate_port = 0;
    int m_gate_host_fd = -1;

    std::string m_node_type; /* current server node type. */
    int m_worker_index = 0;  /* current process index number. */

    /* manager/workers communicate. */
    int m_manager_ctrl_fd = -1; /* channel for send msg. */
    int m_manager_data_fd = -1; /* channel for transfer fd. */

    Nodes* m_nodes = nullptr;           /* server nodes. ketama nodes manager. */
    NodeConn* m_nodes_conn = nullptr;   /* node connection pool. */
    Coroutines* m_coroutines = nullptr; /* coroutines pool. */
    ModuleMgr* m_module_mgr = nullptr;  /* modules so. */
    MysqlMgr* m_mysql_mgr = nullptr;    /* mysql pool. */
    RedisMgr* m_redis_mgr = nullptr;    /* redis pool. */
    ZkClient* m_zk_cli = nullptr;       /* zookeeper client. */
    Payload m_payload;                  /* pro's payload data. */
    SysCmd* m_sys_cmd = nullptr;        /* for node communication.  */
};

}  // namespace kim

#endif  // __KIM_NETWORK_H__