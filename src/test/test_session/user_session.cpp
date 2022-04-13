#include "user_session.h"

namespace kim {

UserSession::UserSession(std::shared_ptr<Log> logger, std::shared_ptr<INet> net, const std::string& id)
    : Session(logger, net, id) {
}

}  // namespace kim