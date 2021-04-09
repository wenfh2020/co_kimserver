#ifndef __KIM_NET_H__
#define __KIM_NET_H__

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
class Request;
class Connection;
class SessionMgr;

class INet {
   public:
    INet() {}
    virtual ~INet() {}

    virtual uint64_t new_seq() { return 0; }
    virtual uint64_t now(bool force = false) { return mstime(); }

    virtual CJsonObject* config() { return nullptr; }
    virtual MysqlMgr* mysql_mgr() { return nullptr; }
    virtual RedisMgr* redis_mgr() { return nullptr; }
    virtual SysCmd* sys_cmd() { return nullptr; }
    virtual ZkClient* zk_client() { return nullptr; }
    virtual WorkerDataMgr* worker_data_mgr() { return nullptr; }
    virtual SessionMgr* session_mgr() { return nullptr; }

    /* for cluster. */
    virtual Nodes* nodes() { return nullptr; }

    /* pro’s type (manager/worker). */
    virtual bool is_worker() { return false; }
    virtual bool is_manager() { return false; }

    virtual int node_port() { return 0; }
    virtual int worker_index() { return -1; }
    virtual std::string node_type() { return ""; }
    virtual std::string node_host() { return ""; }

    virtual void close_fd(int fd) {}
    virtual bool close_conn(uint64_t id) { return false; }
    virtual bool close_conn(Connection* c) { return false; }
    virtual Connection* create_conn(int fd) { return nullptr; }

    /* tcp send. */
    virtual int send_to(Connection* c, const MsgHead& head, const MsgBody& body) { return ERR_FAILED; }
    virtual int send_to(const fd_t& ft, const MsgHead& head, const MsgBody& body) { return ERR_FAILED; }
    virtual int send_ack(const Request* req, int err, const std::string& errstr = "", const std::string& data = "") { return ERR_FAILED; }
    virtual int send_ack(const Request* req, const MsgHead& head, const MsgBody& body) { return ERR_FAILED; }
    virtual int send_req(Connection* c, uint32_t cmd, uint32_t seq, const std::string& data) { return ERR_FAILED; }
    virtual int send_req(const fd_t& ft, uint32_t cmd, uint32_t seq, const std::string& data) { return ERR_FAILED; }

    /* send to other node. */
    virtual int auto_send(const std::string& ip, int port, int worker_index, const MsgHead& head, const MsgBody& body) { return false; }
    /* only for worker. */
    virtual int relay_to_node(const std::string& node_type, const std::string& obj, MsgHead* head, MsgBody* body, MsgHead* head_out, MsgBody* body_out) { return false; }
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
    Net(INet* net) : m_net(net) {}
    virtual ~Net() {}

    INet* net() { return m_net; }
    void set_net(INet* net) { m_net = net; }

   protected:
    INet* m_net = nullptr;
};

}  // namespace kim

#endif  //__KIM_NET_H__
