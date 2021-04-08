#ifndef __KIM_USER_SESSION_H__
#define __KIM_USER_SESSION_H__

#include "session.h"

namespace kim {

class UserSession : public Session {
   public:
    UserSession(Log* logger, INet* net, const std::string& sessid);
    virtual ~UserSession() {}

    uint64_t user_id() { return m_user_id; }
    void set_user_id(uint64_t user_id) { m_user_id = user_id; }

    std::string user_name() { return m_user_name; }
    const std::string& user_name() const { return m_user_name; }
    void set_user_name(const std::string& name) { m_user_name = name; }

   protected:
    uint64_t m_user_id;
    std::string m_user_name;
};

}  // namespace kim

#endif  //__KIM_USER_SESSION_H__