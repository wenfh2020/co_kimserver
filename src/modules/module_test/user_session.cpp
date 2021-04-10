#include "user_session.h"

namespace kim {

UserSession::UserSession(Log* logger, INet* net, const std::string& sessid)
    : Session(logger, net, sessid) {
}

void UserSession::on_timeout() {
    LOG_DEBUG("session timeout, sessid: %s, user id: %d",
              sessid(), m_user_id);
}

}  // namespace kim