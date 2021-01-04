#include "manager.h"

#include "util/set_proc_title.h"

namespace kim {

Manager::Manager() {
}

Manager::~Manager() {
    destory();
}

void Manager::destory() {
    SAFE_DELETE(m_logger);
}

void Manager::run() {
    if (m_net != nullptr) {
        m_net->run();
    }
}

bool Manager::init(const char* conf_path) {
    char work_path[MAX_PATH];
    if (!getcwd(work_path, sizeof(work_path))) {
        return false;
    }
    m_node_info.set_work_path(work_path);

    if (!load_config(conf_path)) {
        printf("dffdsfsdf\n");
        LOG_ERROR("load config failed! %s", conf_path);
        return false;
    }

    if (!load_logger()) {
        printf("dffdsdddfsdf\n");
        LOG_ERROR("init log failed!");
        return false;
    }

    set_proc_title("%s", m_conf("server_name").c_str());
    LOG_INFO("init manager done!");
    return true;
}

bool Manager::load_logger() {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s",
             m_node_info.work_path().c_str(), m_conf("log_path").c_str());

    m_logger = new Log;
    if (m_logger == nullptr) {
        LOG_ERROR("new log failed!");
        return false;
    }

    if (!m_logger->set_log_path(path)) {
        LOG_ERROR("set log path failed! path: %s", path);
        return false;
    }

    if (!m_logger->set_level(m_conf("log_level").c_str())) {
        LOG_ERROR("invalid log level!");
        return false;
    }

    m_logger->set_worker_index(0);
    m_logger->set_process_type(true);
    return true;
}

bool Manager::load_config(const char* path) {
    CJsonObject conf;
    if (!conf.Load(path)) {
        printf("xxxsssxx %s\n", path);
        LOG_ERROR("load json config failed! %s", path);
        return false;
    }

    m_old_conf = m_conf;
    m_conf = conf;
    m_node_info.set_conf_path(path);

    if (m_old_conf.ToString() != m_conf.ToString()) {
        if (m_old_conf.ToString().empty()) {
            m_node_info.set_worker_cnt(str_to_int(m_conf("worker_cnt")));
            m_node_info.set_node_type(m_conf("node_type"));
            m_node_info.mutable_addr_info()->set_node_host(m_conf("node_host"));
            m_node_info.mutable_addr_info()->set_node_port(str_to_int(m_conf("node_port")));
            m_node_info.mutable_addr_info()->set_gate_host(m_conf("gate_host"));
            m_node_info.mutable_addr_info()->set_gate_port(str_to_int(m_conf("gate_port")));
        }
    }

    return true;
}

std::string Manager::worker_name(int index) {
    return format_str("%s_w_%d", m_conf("server_name").c_str(), index);
}

}  // namespace kim