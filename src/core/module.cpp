#include "module.h"

#include "server.h"
#include "util/util.h"

namespace kim {

Module::Module(Log* logger, INet* net, uint64_t id, const std::string& name)
    : Logger(logger), Net(net), m_id(id), m_name(name) {
    register_handle_func();
}

Module::~Module() {
}

bool Module::init(Log* logger, INet* net, uint64_t id, const std::string& name) {
    set_id(id);
    set_net(net);
    set_name(name);
    set_logger(logger);
    register_handle_func();
    return true;
}

}  // namespace kim