#pragma once

#include "protobuf/proto/http.pb.h"
#include "protobuf/proto/msg.pb.h"
#include "server.h"

namespace kim {

#define RETURN_CHECK_OBJ(obj, type)     \
    if (obj == nullptr) {               \
        obj = std::make_shared<type>(); \
    }                                   \
    return obj

class Msg {
   public:
    Msg() = default;
    Msg(const fd_t& ft, bool is_http = false) : m_ft(ft), m_is_http(is_http) {
    }
    virtual ~Msg() = default;

   public:
    const fd_t& ft() const { return m_ft; }
    const int fd() const { return m_ft.fd; }
    const bool is_http() const { return m_is_http; }

    std::shared_ptr<MsgHead> head() { RETURN_CHECK_OBJ(m_msg_head, MsgHead); }
    std::shared_ptr<MsgBody> body() { RETURN_CHECK_OBJ(m_msg_body, MsgBody); }
    std::shared_ptr<HttpMsg> http_msg() { RETURN_CHECK_OBJ(m_http_msg, HttpMsg); }

   private:
    fd_t m_ft;
    bool m_is_http = false;
    std::shared_ptr<MsgHead> m_msg_head = nullptr;  // protobuf msg head.
    std::shared_ptr<MsgBody> m_msg_body = nullptr;  // protobuf msg body.
    std::shared_ptr<HttpMsg> m_http_msg = nullptr;  // http msg.
};

};  // namespace kim
