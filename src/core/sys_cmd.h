/* doc: https://wenfh2020.com/2020/10/23/kimserver-node-contact/ */
#pragma once

#include "net.h"
#include "nodes.h"
#include "protobuf/sys/payload.pb.h"
#include "request.h"
#include "server.h"
#include "timer.h"

namespace kim {

class SysCmd : public Logger, public Net, public TimerCron {
   public:
    SysCmd(std::shared_ptr<Log> logger, std::shared_ptr<INet> net);
    virtual ~SysCmd() {}

    int send_heart_beat(std::shared_ptr<Connection> c);
    int send_connect_req_to_worker(std::shared_ptr<Connection> c);

    /* worker send data to manager. */
    int send_zk_nodes_version_to_manager(int version);
    int send_payload_to_manager(const Payload& pl);

    /* manager send data to worker. */
    int send_add_zk_node_to_worker(const zk_node& node);
    int send_del_zk_node_to_worker(const std::string& zk_path);
    int send_reg_zk_node_to_worker(const register_node& rn);

    int handle_msg(std::shared_ptr<Request> req);
    virtual void on_repeat_timer() override;

   private:
    int handle_worker_msg(std::shared_ptr<Request> req);
    int handle_manager_msg(std::shared_ptr<Request> req);

    /* zookeeper notice (manager --> worker). */
    int on_req_add_zk_node(std::shared_ptr<Request> req);
    int on_rsp_add_zk_node(std::shared_ptr<Request> req);

    int on_req_del_zk_node(std::shared_ptr<Request> req);
    int on_rsp_del_zk_node(std::shared_ptr<Request> req);

    int on_req_reg_zk_node(std::shared_ptr<Request> req);
    int on_rsp_reg_zk_node(std::shared_ptr<Request> req);

    /* worker --> manager for checking zk nodes's data. */
    int on_req_sync_zk_nodes(std::shared_ptr<Request> req);
    int on_rsp_sync_zk_nodes(std::shared_ptr<Request> req);

    /* worker --> manager. */
    int on_req_update_payload(std::shared_ptr<Request> req);
    int on_rsp_update_payload(std::shared_ptr<Request> req);

    int on_req_connect_to_worker(std::shared_ptr<Request> req);
    int on_rsp_connect_to_worker(std::shared_ptr<Request> req);

    int on_req_tell_worker(std::shared_ptr<Request> req);
    int on_rsp_tell_worker(std::shared_ptr<Request> req);

    int on_req_heart_beat(std::shared_ptr<Request> req);
    int on_rsp_heart_beat(std::shared_ptr<Request> req);

   private:
    int check_rsp(std::shared_ptr<Request> req);
};

}  // namespace kim
