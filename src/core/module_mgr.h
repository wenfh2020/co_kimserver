
#pragma once

#include "module.h"

namespace kim {

class ModuleMgr : public Logger, public Net {
   public:
    ModuleMgr(Log* logger, INet* net);
    virtual ~ModuleMgr();

    bool init(CJsonObject* config);
    Module* get_module(uint64_t id);
    bool reload_so(const std::string& name);

    int handle_request(const Request* req);

   private:
    Module* get_module(const std::string& name);
    bool load_so(const std::string& name, const std::string& path, uint64_t id = 0);
    bool unload_so(const std::string& name);

   private:
    std::unordered_map<uint64_t, Module*> m_modules;  // modules.
};

}  // namespace kim
