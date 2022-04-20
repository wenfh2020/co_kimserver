
#pragma once

#include "module.h"

namespace kim {

class ModuleMgr : public Logger, public Net {
   public:
    ModuleMgr(std::shared_ptr<Log> logger, std::shared_ptr<INet> net);
    virtual ~ModuleMgr();

    bool init(CJsonObject* config);
    bool reload_so(const std::string& name);
    Module* get_module(const std::string& name);

    int handle_request(std::shared_ptr<Msg> req);

   private:
    bool load_so(const std::string& name, const std::string& path);
    bool unload_so(const std::string& name);

   private:
    std::unordered_map<std::string, Module*> m_modules;  // modules.
};

}  // namespace kim
