#pragma once

#include "error.h"
#include "protobuf/proto/msg.pb.h"
#include "request.h"
#include "server.h"
#include "util/so.h"

namespace kim {

/* Module is a container, which is used for cmd's route.*/

class Module : public Logger, public Net, public So {
   public:
    Module() {}
    Module(std::shared_ptr<Log> logger, std::shared_ptr<INet> net, uint64_t id, const std::string& name);
    virtual ~Module();
    bool init(std::shared_ptr<Log> logger, std::shared_ptr<INet> net, uint64_t id, const std::string& name = "");

    void set_id(uint64_t id) { m_id = id; }
    uint64_t id() { return m_id; }

    void set_name(const std::string& name) { m_name = name; }
    const std::string& name() const { return m_name; }
    const char* name() { return m_name.c_str(); }

    virtual void register_handle_func() {}
    virtual int handle_request(std::shared_ptr<Request> req) {
        return ERR_UNKOWN_CMD;
    }
    virtual int filter_request(std::shared_ptr<Request> req) {
        return ERR_UNKOWN_CMD;
    }

   private:
    uint64_t m_id = 0;
    std::string m_name;
};

#define REGISTER_HANDLER(class_name)                                                                                  \
   public:                                                                                                            \
    class_name() {}                                                                                                   \
    class_name(std::shared_ptr<Log> logger, std::shared_ptr<INet> net, uint64_t id = 0, const std::string& name = "") \
        : Module(logger, net, id, name) {                                                                             \
    }                                                                                                                 \
    typedef int (class_name::*cmd_func)(std::shared_ptr<Request> req);                                                \
    virtual int handle_request(std::shared_ptr<Request> req) {                                                        \
        auto it = m_cmd_funcs.find(req->msg_head()->cmd());                                                           \
        if (it == m_cmd_funcs.end()) {                                                                                \
            return filter_request(req);                                                                               \
        }                                                                                                             \
        return (this->*(it->second))(req);                                                                            \
    }                                                                                                                 \
                                                                                                                      \
   protected:                                                                                                         \
    std::unordered_map<int, cmd_func> m_cmd_funcs;

#define HANDLE_PROTO_FUNC(id, func) \
    m_cmd_funcs[id] = &func;

}  // namespace kim
