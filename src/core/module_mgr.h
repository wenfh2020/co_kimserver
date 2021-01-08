
#ifndef __KIM_MODULE_MGR_H__
#define __KIM_MODULE_MGR_H__

#include "module.h"
#include "util/json/CJsonObject.hpp"

namespace kim {

class ModuleMgr : Base {
   public:
    ModuleMgr(Log* logger, INet* net);
    virtual ~ModuleMgr();

    bool init(CJsonObject& config);
    Module* get_module(uint64_t id);
    bool reload_so(const std::string& name);

    int handle_request(const fd_t& fdata, const MsgHead& head, const MsgBody& body);

   private:
    Module* get_module(const std::string& name);
    bool load_so(const std::string& name, const std::string& path, uint64_t id = 0);
    bool unload_so(const std::string& name);

   private:
    std::unordered_map<uint64_t, Module*> m_modules;  // modules.
};

}  // namespace kim

#endif  //__KIM_MODULE_MGR_H__