#pragma once

#include "error.h"
#include "mysql/mysql_mgr.h"
#include "protobuf/proto/http.pb.h"
#include "protobuf/proto/msg.pb.h"
#include "server.h"
#include "util/util.h"

namespace kim {

class Nodes;
class WorkerDataMgr;
class SysCmd;
class RedisMgr;
class ZkClient;
class Coroutines;
class Msg;
class Connection;
class SessionMgr;

class INet : public std::enable_shared_from_this<INet> {
   public:
    INet() {}
    virtual ~INet() {}

    virtual uint64_t new_seq() { return 0; }
    virtual uint64_t now(bool force = false) { return mstime(); }

    virtual CJsonObject* config() { return nullptr; }
    virtual std::shared_ptr<MysqlMgr> mysql_mgr() { return nullptr; }
    virtual std::shared_ptr<RedisMgr> redis_mgr() { return nullptr; }
    virtual std::shared_ptr<SysCmd> sys_cmd() { return nullptr; }
    virtual std::shared_ptr<ZkClient> zk_client() { return nullptr; }
    virtual std::shared_ptr<WorkerDataMgr> worker_data_mgr() { return nullptr; }
    virtual std::shared_ptr<SessionMgr> session_mgr() { return nullptr; }

    /* for cluster. */
    virtual std::shared_ptr<Nodes> nodes() { return nullptr; }

    /* pro’s type (manager/worker). */
    virtual bool is_worker() { return false; }
    virtual bool is_manager() { return false; }

    virtual int node_port() { return 0; }
    virtual int worker_index() { return -1; }
    virtual std::string node_type() { return ""; }
    virtual std::string node_host() { return ""; }

    virtual void close_fd(int fd) {}
    virtual bool close_conn(uint64_t id) { return false; }
    virtual bool close_conn(std::shared_ptr<Connection> c) { return false; }
    virtual std::shared_ptr<Connection> create_conn(int fd) { return nullptr; }

    /* tcp send. */
    virtual int send_to(const fd_t& ft, std::shared_ptr<Msg> msg) { return ERR_FAILED; }
    virtual int send_to(std::shared_ptr<Connection> c, std::shared_ptr<Msg> msg) { return ERR_FAILED; }
    virtual int send_ack(std::shared_ptr<Msg> req, int err, const std::string& errstr = "", const std::string& data = "") { return ERR_FAILED; }
    virtual int send_req(std::shared_ptr<Connection> c, uint32_t cmd, uint32_t seq, const std::string& data) { return ERR_FAILED; }
    virtual int send_req(const fd_t& ft, uint32_t cmd, uint32_t seq, const std::string& data) { return ERR_FAILED; }

    /* only for worker. */
    virtual int relay_to_node(const std::string& node_type, const std::string& obj,
                              std::shared_ptr<Msg> req, std::shared_ptr<Msg> ack) { return ERR_FAILED; }
    /* only for worker. */
    virtual int send_to_manager(int cmd, uint64_t seq, const std::string& data) { return ERR_FAILED; }
    /* only for manager. */
    virtual int send_to_workers(int cmd, uint64_t seq, const std::string& data) { return ERR_FAILED; }

    /* connection. */
    virtual bool update_conn_state(const fd_t& ft, int state) { return false; }
    virtual bool add_client_conn(const std::string& node_id, const fd_t& ft) { return false; }
};

class Net {
   public:
    Net() {}
    Net(std::shared_ptr<INet> net) : m_net(net) {}
    virtual ~Net() {}

    std::shared_ptr<INet> net() { return m_net.lock(); }
    void set_net(std::shared_ptr<INet> net) { m_net = net; }

   protected:
    std::weak_ptr<INet> m_net;
};

}  // namespace kim
