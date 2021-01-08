#ifndef __KIM_MODULE_H__
#define __KIM_MODULE_H__

#include "base.h"
#include "error.h"
#include "protobuf/proto/msg.pb.h"
#include "server.h"
#include "util/so.h"

namespace kim {

/* Module is a container, which is used for cmd's route.*/

class Module : public Base, public So {
   public:
    Module() {}
    Module(Log* logger, uint64_t id, const std::string& name);
    virtual ~Module();
    bool init(Log* logger, uint64_t id, const std::string& name = "");

    virtual void register_handle_func() {}
    int handle_request(const fd_t& fdata, const MsgHead& head, const MsgBody& body) { return ERR_UNKOWN_CMD; }
};

}  // namespace kim

#endif  //__KIM_MODULE_H__