#include "sys_config.h"

#include "server.h"

namespace kim {

SysConfig::~SysConfig() {
    SAFE_DELETE(m_config);
}

bool SysConfig::init(const std::string& config_path) {
    if (access(config_path.c_str(), W_OK) == -1) {
        std::cerr << "invalid config path: "
                  << config_path
                  << std::endl;
        return false;
    }

    char work_path[MAX_PATH];
    if (!getcwd(work_path, sizeof(work_path))) {
        std::cerr << "get work path failed!" << std::endl;
        return false;
    }
    m_work_path = work_path;

    /* init config. */
    if (!load_config(config_path)) {
        std::cerr << "load config failed! conf path: "
                  << config_path
                  << std::endl;
        return false;
    }

    m_conf_path = config_path;
    return true;
}

bool SysConfig::load_config(const std::string& config_path) {
    m_config = new CJsonObject;
    if (!m_config->Load(config_path)) {
        SAFE_DELETE(m_config);
        std::cerr << "load config json failed! path: "
                  << config_path
                  << std::endl;
        return false;
    }
    return true;
}

bool SysConfig::is_reuseport() {
    bool ret = false;
    m_config->Get("is_reuseport", ret);
    return ret;
}

bool SysConfig::is_open_zookeeper() {
    bool ret = false;
    m_config->Get("zookeeper").Get("is_open", ret);
    return ret;
}

}  // namespace kim