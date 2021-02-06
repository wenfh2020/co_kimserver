#ifndef __KIM_MODULE_H__
#define __KIM_MODULE_H__

#include "base.h"
#include "error.h"
#include "protobuf/proto/msg.pb.h"
#include "request.h"
#include "server.h"
#include "util/so.h"

namespace kim {

/* Module is a container, which is used for cmd's route.*/

class Module : public Base, public So {
   public:
    Module() {}
    Module(Log* logger, INet* net, uint64_t id, const std::string& name);
    virtual ~Module();
    bool init(Log* logger, INet* net, uint64_t id, const std::string& name = "");

    virtual void register_handle_func() {}
    virtual int handle_request(const Request* req) {
        return ERR_UNKOWN_CMD;
    }
    virtual int filter_request(const Request* req) {
        return ERR_UNKOWN_CMD;
    }
};

#define REGISTER_HANDLER(class_name)                                                  \
   public:                                                                            \
    class_name() {}                                                                   \
    class_name(Log* logger, INet* net, uint64_t id = 0, const std::string& name = "") \
        : Module(logger, net, id, name) {                                             \
    }                                                                                 \
    typedef int (class_name::*cmd_func)(const Request* req);                          \
    virtual int handle_request(const Request* req) {                                  \
        auto it = m_cmd_funcs.find(req->msg_head()->cmd());                           \
        if (it == m_cmd_funcs.end()) {                                                \
            return filter_request(req);                                               \
        }                                                                             \
        return (this->*(it->second))(req);                                            \
    }                                                                                 \
                                                                                      \
   protected:                                                                         \
    std::unordered_map<int, cmd_func> m_cmd_funcs;

#define HANDLE_PROTO_FUNC(id, func) \
    m_cmd_funcs[id] = &func;

}  // namespace kim

#endif  //__KIM_MODULE_H__