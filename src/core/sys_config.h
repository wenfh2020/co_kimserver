#ifndef __SYS_CONFIG_H__
#define __SYS_CONFIG_H__

#include "util/json/CJsonObject.hpp"
#include "util/util.h"

namespace kim {

class SysConfig {
   public:
    SysConfig() {}
    virtual ~SysConfig();

    bool init(const std::string& config_path);
    const std::string& config_path() const { return m_conf_path; }

    bool load_config(const std::string& config_path);
    CJsonObject* config() { return m_config; }

    void set_work_path(const std::string& path) { m_work_path = path; }
    const std::string& work_path() const { return m_work_path; }

    int worker_cnt() { return str_to_int((*m_config)("worker_cnt")); }
    std::string server_name() { return (*m_config)("server_name"); }
    std::string worker_name(int worker_index) {
        return format_str("%s_w_%d", server_name().c_str(), worker_index);
    }

    std::string log_path() { return (*m_config)("log_path"); }
    std::string log_level() { return (*m_config)("log_level"); }

    int keep_alive() { return str_to_int((*m_config)("keep_alive")); }

    std::string node_type() { return (*m_config)("node_type"); }
    std::string node_host() { return (*m_config)("node_host"); }
    int node_port() { return str_to_int((*m_config)("node_port")); }

    std::string gate_codec() { return (*m_config)("gate_codec"); }
    std::string gate_host() { return (*m_config)("gate_host"); }
    int gate_port() { return str_to_int((*m_config)("gate_port")); }
    int max_clients() { return str_to_int((*m_config)("max_clients")); }

    bool is_reuseport();
    bool is_open_zookeeper();

   protected:
    CJsonObject* m_config = nullptr;
    std::string m_work_path;
    std::string m_conf_path;
};

}  // namespace kim

#endif  //__SYS_CONFIG_H__