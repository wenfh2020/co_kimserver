#include "module.h"

#include "server.h"
#include "util/util.h"

namespace kim {

Module::Module(std::shared_ptr<Log> logger, std::shared_ptr<INet> net, const std::string& name)
    : Logger(logger), Net(net), m_name(name) {
    register_handle_func();
}

Module::~Module() {
}

bool Module::init(std::shared_ptr<Log> logger, std::shared_ptr<INet> net, const std::string& name) {
    set_net(net);
    set_name(name);
    set_logger(logger);
    register_handle_func();
    return true;
}

}  // namespace kim