#include "module_mgr.h"

#include <dlfcn.h>

#include "util/util.h"

#define MODULE_DIR "/modules/"
#define DL_ERROR() (dlerror() != nullptr) ? dlerror() : "unknown error"

namespace kim {

typedef Module* CreateModule();

ModuleMgr::ModuleMgr(std::shared_ptr<Log> log, std::shared_ptr<INet> net)
    : Logger(log), Net(net) {
}

ModuleMgr::~ModuleMgr() {
    for (const auto& it : m_modules) {
        auto module = it.second;
        if (dlclose(module->so_handle()) == -1) {
            LOG_ERROR("close so failed! so: %s, errstr: %s",
                      module->name(), DL_ERROR());
        }
        SAFE_DELETE(module);
    }
    m_modules.clear();
}

bool ModuleMgr::init(CJsonObject* config) {
    std::string name, path;
    CJsonObject& array = (*config)["modules"];

    for (int i = 0; i < array.GetArraySize(); i++) {
        name = array(i);
        path = work_path() + MODULE_DIR + name;
        LOG_DEBUG("loading so: %s, path: %s!", name.c_str(), path.c_str());

        if (0 != access(path.c_str(), F_OK)) {
            LOG_WARN("%s not exist!", path.c_str());
            return false;
        }

        if (!load_so(name, path)) {
            LOG_CRIT("load so: %s failed!", name.c_str());
            return false;
        }
        LOG_DEBUG("loading so: %s, path: %s done!", name.c_str(), path.c_str());
    }

    return true;
}

bool ModuleMgr::load_so(const std::string& name, const std::string& path) {
    auto module = get_module(name);
    if (module != nullptr) {
        LOG_ERROR("duplicate load so: %s", name.c_str());
        return false;
    }

    /* load so. */
    auto handle = dlopen(path.c_str(), RTLD_NOW);
    if (handle == nullptr) {
        LOG_ERROR("open so failed! so: %s, errstr: %s", path.c_str(), DL_ERROR());
        return false;
    }

    auto create_module = (CreateModule*)dlsym(handle, "create");
    if (create_module == nullptr) {
        LOG_ERROR("open so failed! so: %s, errstr: %s", path.c_str(), DL_ERROR());
        if (dlclose(handle) == -1) {
            LOG_ERROR("close so failed! so: %s, errstr: %s",
                      module->name(), DL_ERROR());
        }
        return false;
    }

    module = (Module*)create_module();
    if (!module->init(logger(), net(), name)) {
        LOG_ERROR("init module failed! module: %s", name.c_str());
        if (dlclose(handle) == -1) {
            LOG_ERROR("close so failed! so: %s, errstr: %s",
                      module->name(), DL_ERROR());
        }
        return false;
    }

    module->set_name(name);
    module->set_so_path(path);
    module->set_so_handle(handle);
    m_modules[name] = module;
    LOG_INFO("load so: %s done!", name.c_str());
    return true;
}

bool ModuleMgr::reload_so(const std::string& name) {
    std::string path = work_path() + MODULE_DIR + name;
    LOG_DEBUG("reloading so: %s, path: %s!", name.c_str(), path.c_str());

    if (access(path.c_str(), F_OK) != 0) {
        LOG_WARN("%s not exist!", path.c_str());
        return false;
    }

    auto module = get_module(name);
    if (module != nullptr) {
        unload_so(name);
    }

    return load_so(name, path);
}

bool ModuleMgr::unload_so(const std::string& name) {
    auto module = get_module(name);
    if (module == nullptr) {
        LOG_ERROR("find so: %s failed!", name.c_str());
        return false;
    }

    if (dlclose(module->so_handle()) == -1) {
        LOG_ERROR("close so failed! so: %s, errstr: %s",
                  module->name(), DL_ERROR());
    }

    auto it = m_modules.find(module->name());
    if (it != m_modules.end()) {
        m_modules.erase(it);
    } else {
        LOG_ERROR("find module: %s failed!", name.c_str());
    }
    SAFE_DELETE(module);

    LOG_INFO("unload module so: %s", name.c_str());
    return true;
}

Module* ModuleMgr::get_module(const std::string& name) {
    for (const auto& it : m_modules) {
        auto module = it.second;
        if (module->name() == name) {
            return module;
        }
    }
    return nullptr;
}

int ModuleMgr::handle_request(std::shared_ptr<Msg> req) {
    int ret = ERR_UNKOWN_CMD;

    for (const auto& it : m_modules) {
        auto module = it.second;
        LOG_TRACE("module name: %s", module->name());
        ret = module->handle_request(req);
        if (ret != ERR_UNKOWN_CMD) {
            return ret;
        }
    }

    return ret;
}

}  // namespace kim