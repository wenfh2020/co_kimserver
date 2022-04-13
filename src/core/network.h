#pragma once

#include "codec/codec.h"
#include "connection.h"
#include "coroutines.h"
#include "module_mgr.h"
#include "net.h"
#include "net/anet.h"
#include "net/channel.h"
#include "node_connection.h"
#include "nodes.h"
#include "session.h"
#include "sys_cmd.h"
#include "sys_config.h"
#include "util/json/CJsonObject.hpp"
#include "worker_data_mgr.h"
#include "zk_client.h"

namespace kim {

class Network : public INet, public TimerCron, public Logger {
   public:
    enum class TYPE {
        UNKNOWN = 0,
        MANAGER,
        WORKER,
    };

    Network(std::shared_ptr<Log> logger, TYPE type);
    virtual ~Network();

    Network(const Network&) = delete;
    Network& operator=(const Network&) = delete;

    /* for manager. */
    bool create_m(std::shared_ptr<SysConfig> config);
    /* for worker.  */
    bool create_w(std::shared_ptr<SysConfig> config, int ctrl_fd, int data_fd, int index);

    bool init_manager_channel(fd_t& fctrl, fd_t& fdata);

    /* events. */
    void run();
    void exit_libco();
    void destory();

    bool set_gate_codec(const std::string& codec);
    void set_keep_alive(uint64_t ms) { m_keep_alive = ms; }
    uint64_t keep_alive() { return m_keep_alive; }
    bool is_request(int cmd) { return (cmd & 0x00000001); }

    virtual uint64_t now(bool force = false) override;
    virtual uint64_t new_seq() override { return ++m_seq; }
    virtual CJsonObject* config() override { return m_config->config(); }

    /* pro's type. */
    virtual bool is_worker() override { return m_type == TYPE::WORKER; }
    virtual bool is_manager() override { return m_type == TYPE::MANAGER; }

    virtual int worker_index() override { return m_worker_index; }
    virtual int node_port() override { return m_config->gate_port(); }
    virtual std::string node_type() override { return m_config->node_type(); }
    virtual std::string node_host() override { return m_config->node_host(); }

    virtual std::shared_ptr<Nodes> nodes() override { return m_nodes; }
    virtual std::shared_ptr<SysCmd> sys_cmd() override { return m_sys_cmd; }
    virtual std::shared_ptr<MysqlMgr> mysql_mgr() override { return m_mysql_mgr; }
    virtual std::shared_ptr<RedisMgr> redis_mgr() override { return m_redis_mgr; }
    virtual std::shared_ptr<WorkerDataMgr> worker_data_mgr() override { return m_worker_data_mgr; }
    virtual std::shared_ptr<ZkClient> zk_client() override { return m_zk_cli; }
    virtual std::shared_ptr<SessionMgr> session_mgr() override { return m_session_mgr; }

    virtual int send_to(const fd_t& ft, const MsgHead& head, const MsgBody& body) override;
    virtual int send_to(std::shared_ptr<Connection> c, const MsgHead& head, const MsgBody& body) override;
    virtual int send_req(const fd_t& ft, uint32_t cmd, uint32_t seq, const std::string& data) override;
    virtual int send_req(std::shared_ptr<Connection> c, uint32_t cmd, uint32_t seq, const std::string& data) override;
    virtual int send_ack(std::shared_ptr<Request> req, int err, const std::string& errstr = "", const std::string& data = "") override;

    // virtual int auto_send(const std::string& ip, int port, int worker_index, const MsgHead& head, const MsgBody& body) override;
    virtual int relay_to_node(const std::string& node_type, const std::string& obj, MsgHead* head, MsgBody* body, MsgHead* head_out, MsgBody* body_out) override;

    /* channel. */
    virtual int send_to_manager(int cmd, uint64_t seq, const std::string& data) override;
    virtual int send_to_workers(int cmd, uint64_t seq, const std::string& data) override;

    /* connection. */
    virtual bool update_conn_state(const fd_t& ft, int state) override;
    virtual bool add_client_conn(const std::string& node_id, const fd_t& ft) override;

    /* use in fork. */
    void close_fds();
    void close_channel(int* fds); /* close socketpair. */

    /* connection. */
    virtual void close_fd(int fd) override;
    virtual bool close_conn(uint64_t id) override;
    virtual bool close_conn(std::shared_ptr<Connection> c) override;
    virtual std::shared_ptr<Connection> create_conn(int fd) override;

    inline bool is_valid_conn(uint64_t id);
    inline bool is_valid_conn(std::shared_ptr<Connection> c);

    std::shared_ptr<Connection> get_conn(const fd_t& ft);
    Codec::STATUS conn_write_data(std::shared_ptr<Connection> c);
    std::shared_ptr<Connection> create_conn(int fd, Codec::TYPE codec, bool is_channel = false);
    std::shared_ptr<Connection> get_node_conn(const std::string& host, int port, int worker_index);

    void clear_routines();
    virtual void on_repeat_timer() override; /* call by parent, 10 times/s on Linux. */

    /* payload. */
    bool report_payload_to_manager();
    bool report_payload_to_zookeeper();

   private:
    bool load_config(std::shared_ptr<SysConfig> config);
    bool load_public(std::shared_ptr<SysConfig> config);
    bool load_worker_data_mgr();
    bool load_modules();
    bool load_corotines();
    bool load_zk_mgr(); /* zookeeper client. */
    bool load_nodes_conn();
    bool load_mysql_mgr();
    bool load_redis_mgr();
    bool load_session_mgr();
    bool ensure_files_limit();

    /* socket & connection. */
    int listen_to_port(const char* host, int port, bool is_reuseport = false);

    int process_msg(std::shared_ptr<Connection> c);
    int process_tcp_msg(std::shared_ptr<Connection> c);
    int process_http_msg(std::shared_ptr<Connection> c);

    /* coroutines. */
    void on_handle_accept_nodes_conn();
    void on_handle_accept_gate_conn();
    void on_handle_read_transfer_fd(int fd);
    void on_handle_requests(std::shared_ptr<Connection> c);

   private:
    std::shared_ptr<SysConfig> m_config = nullptr;   /* system config data. */
    Codec::TYPE m_gate_codec = Codec::TYPE::UNKNOWN; /* gate codec type. */

    std::unordered_map<int, uint64_t> m_fd_conns;                              /* key fd, value: conn id. */
    std::unordered_map<uint64_t, std::shared_ptr<Connection>> m_conns;         /* key: fd, value: connection. */
    std::unordered_map<std::string, std::shared_ptr<Connection>> m_node_conns; /* key: node_id, value: connection. */

    uint64_t m_seq = 0;          /* incremental serial number. */
    char m_errstr[ANET_ERR_LEN]; /* error string. */

    int m_time_index = 0;
    uint64_t m_now_time = 0;

    TYPE m_type = TYPE::UNKNOWN;                                /* owner type. */
    uint64_t m_keep_alive = IO_TIMEOUT_VAL;                     /* io timeout. */
    std::shared_ptr<WorkerDataMgr> m_worker_data_mgr = nullptr; /* manager handle worker data. */

    /* node for inner servers. */
    int m_node_fd = -1;

    int m_max_clients = (1024 - CONFIG_MIN_RESERVED_FDS);

    int m_gate_fd = -1;     /* gate for client, listen fd. */
    int m_worker_index = 0; /* current process index number. */

    /* manager/workers communicate, used by worker. */
    fd_t m_manager_fctrl; /* channel for send message. */
    fd_t m_manager_fdata; /* channel for transfer fd. */

    Payload m_payload; /* pro's payload data. */

    std::shared_ptr<Nodes> m_nodes = nullptr;            /* server nodes. ketama nodes manager. */
    std::unique_ptr<NodeConn> m_nodes_conn = nullptr;    /* node connection pool. */
    std::unique_ptr<Coroutines> m_coroutines = nullptr;  /* coroutines pool. */
    std::unique_ptr<ModuleMgr> m_module_mgr = nullptr;   /* modules so. */
    std::shared_ptr<MysqlMgr> m_mysql_mgr = nullptr;     /* mysql pool. */
    std::shared_ptr<RedisMgr> m_redis_mgr = nullptr;     /* redis pool. */
    std::shared_ptr<ZkClient> m_zk_cli = nullptr;        /* zookeeper client. */
    std::shared_ptr<SysCmd> m_sys_cmd = nullptr;         /* for node communication.  */
    std::shared_ptr<SessionMgr> m_session_mgr = nullptr; /* session pool. */
};

}  // namespace kim
