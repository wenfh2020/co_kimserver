#include "sys_config.h"

#include "server.h"

namespace kim {

SysConfig::~SysConfig() {
    SAFE_DELETE(m_config);
}

bool SysConfig::init(const std::string& conf_path) {
    if (access(conf_path.c_str(), W_OK) == -1) {
        std::cerr << "invalid config path: "
                  << conf_path
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
    if (!load_config(conf_path)) {
        std::cerr << "load config failed! conf path: "
                  << conf_path
                  << std::endl;
        return false;
    }

    m_conf_path = conf_path;
    return true;
}

bool SysConfig::load_config(const std::string& conf_path) {
    m_config = new CJsonObject;
    if (!m_config->Load(conf_path)) {
        SAFE_DELETE(m_config);
        std::cerr << "load config json failed! path: "
                  << conf_path
                  << std::endl;
        return false;
    }
    return true;
}

}  // namespace kim