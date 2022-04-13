#pragma once

#include "network.h"
#include "nodes.h"
#include "timer.h"
#include "util/json/CJsonObject.hpp"
#include "worker_data_mgr.h"

namespace kim {

class Worker : CoTimer {
   public:
    Worker(const std::string& name);
    virtual ~Worker();

    bool init(const worker_info_t* info, const std::string& config_path);
    void run();

    virtual void on_repeat_timer() override;

   private:
    bool load_logger();
    bool load_network();
    bool load_sys_config(const std::string& config_path);

    /* signals. */
    void load_signals();
    void signal_handler_event(int sig);
    static void signal_handler(int sig);

   private:
    std::shared_ptr<Log> m_logger = nullptr;       /* logger. */
    std::shared_ptr<Network> m_net = nullptr;      /* network. */
    std::shared_ptr<SysConfig> m_config = nullptr; /* system config data. */
    worker_info_t m_worker_info;                   /* current worker info. */
    static void* m_signal_user_data;
};

}  // namespace kim
