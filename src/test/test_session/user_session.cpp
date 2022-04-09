#include "user_session.h"

namespace kim {

UserSession::UserSession(Log* logger, INet* net, const std::string& id)
    : Session(logger, net, id) {
}

}  // namespace kim