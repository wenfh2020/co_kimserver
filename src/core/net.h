#ifndef __KIM_NET_H__
#define __KIM_NET_H__

#include "error.h"
#include "mysql/mysql_mgr.h"
#include "protobuf/proto/http.pb.h"
#include "protobuf/proto/msg.pb.h"
#include "request.h"
#include "server.h"
#include "util/util.h"

namespace kim {

class Nodes;
class WorkerDataMgr;

class INet {
   public:
    INet() {}
    virtual ~INet() {}

    virtual uint64_t now() { return mstime(); }
    virtual uint64_t new_seq() { return 0; }
    virtual CJsonObject* config() { return nullptr; }
    virtual MysqlMgr* mysql_mgr() { return nullptr; }
    virtual WorkerDataMgr* worker_data_mgr() { return nullptr; }

    /* for cluster. */
    virtual Nodes* nodes() { return nullptr; }

    /* pro’s type (manager/worker). */
    virtual bool is_worker() { return false; }
    virtual bool is_manager() { return false; }

    virtual std::string node_type() { return ""; }
    virtual std::string node_host() { return ""; }
    virtual int node_port() { return 0; }
    virtual int worker_index() { return -1; }

    /* tcp send. */
    virtual int send_to(Connection* c, const MsgHead& head, const MsgBody& body) { return ERR_FAILED; }
    virtual int send_to(const fd_t& f, const MsgHead& head, const MsgBody& body) { return ERR_FAILED; }
    virtual int send_ack(const Request* req, int err, const std::string& errstr = "", const std::string& data = "") { return ERR_FAILED; }
    virtual int send_req(Connection* c, uint32_t cmd, uint32_t seq, const std::string& data) { return ERR_FAILED; }
    virtual int send_req(const fd_t& f, uint32_t cmd, uint32_t seq, const std::string& data) { return ERR_FAILED; }

    /* only for worker. */
    virtual int send_to_manager(int cmd, uint64_t seq, const std::string& data) { return ERR_FAILED; }
};

}  // namespace kim

#endif  //__KIM_NET_H__
