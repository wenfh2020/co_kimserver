#pragma once

#include "session.h"

namespace kim {

class UserSession : public Session {
   public:
    UserSession(Log* logger, INet* net, const std::string& id);
    virtual ~UserSession() {}

    uint64_t user_id() { return m_user_id; }
    void set_user_id(uint64_t user_id) { m_user_id = user_id; }

    std::string user_name() { return m_user_name; }
    const std::string& user_name() const { return m_user_name; }
    void set_user_name(const std::string& name) { m_user_name = name; }

   public:
    virtual void on_timeout() override;

   protected:
    uint64_t m_user_id;
    std::string m_user_name;
};

}  // namespace kim
