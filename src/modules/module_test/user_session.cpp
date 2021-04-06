#include "user_session.h"

namespace kim {

UserSession::UserSession(
    Log* logger, INet* net, const std::string& sessid, uint64_t alive)
    : Session(logger, net, sessid, alive) {
}

}  // namespace kim