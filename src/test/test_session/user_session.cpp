#include "user_session.h"

namespace kim {

UserSession::UserSession(Log* logger, INet* net, const std::string& sessid)
    : Session(logger, net, sessid) {
}

}  // namespace kim