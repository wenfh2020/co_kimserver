/* https://wenfh2020.com/2020/10/24/kimserver-nodes-discovery/ */

#pragma once

#include "net.h"
#include "nodes.h"
#include "timer.h"
#include "util/json/CJsonObject.hpp"
#include "util/log.h"
#include "zookeeper/zk.h"
#include "zookeeper/zk_bio.h"

namespace kim {

class ZkClient : public Bio, public Net, public TimerCron {
   public:
    ZkClient(std::shared_ptr<Log> logger, std::shared_ptr<INet> net);
    virtual ~ZkClient();

    bool init(const CJsonObject& config);

   public:
    /* connect to zk servers. */
    bool connect(const std::string& servers);
    /* when notify expired, reconnect to zookeeper. */
    bool reconnect();
    /* set zk log before connect. */
    bool set_zk_log(const std::string& path, const std::string& ll);

    /* (sync) call by bio thread. */
    virtual void bio_process_cmd(zk_task_t* task) override;
    /* (async) timer. */
    virtual void on_repeat_timer() override;
    /* (async) call by timer. */
    virtual void timer_process_ack(zk_task_t* task) override;

    /* payload data. */
    bool set_payload_data(const std::string& data);
    void close_my_node();

    /* callback by zookeeper-client-c. */
    static void on_zookeeper_watch_events(zhandle_t* zh, int type, int state, const char* path, void* privdata);
    void on_zk_watch_events(int type, int state, const char* path, void* privdata);

    /* call by timer. */
    void on_zk_register(const kim::zk_task_t* task);
    void on_zk_get_data(const kim::zk_task_t* task);
    void on_zk_set_data(const kim::zk_task_t* task);
    void on_zk_data_change(const kim::zk_task_t* task);
    void on_zk_child_change(const kim::zk_task_t* task);
    void on_zk_node_deleted(const kim::zk_task_t* task);
    void on_zk_node_created(const kim::zk_task_t* task);
    void on_zk_session_connected(const kim::zk_task_t* task);
    void on_zk_session_connecting(const kim::zk_task_t* task);
    void on_zk_session_expired(const kim::zk_task_t* task);

   private:
    /* register node to zookeeper. */
    bool node_register();
    /* check and create parent before register nodes. */
    utility::zoo_rc bio_create_parent(const std::string& parent);
    /* register node to zk. */
    utility::zoo_rc bio_register_node(zk_task_t* task);

   private:
    CJsonObject m_config;

    /* zk. */
    utility::zk_cpp* m_zk = nullptr;
    bool m_is_connected = false;
    bool m_is_registered = false;
    bool m_is_expired = false;

    std::string m_payload_node_path;
};

}  // namespace kim
