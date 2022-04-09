#pragma once

#include "connection.h"
#include "protobuf/proto/http.pb.h"
#include "protobuf/proto/msg.pb.h"

namespace kim {

class Request {
   public:
    Request(const fd_t& ft, bool is_http = false);
    virtual ~Request();

    Request(const Request& req) = delete;
    Request() = delete;
    Request& operator=(const Request&) = delete;

   public:
    const fd_t& ft() const { return m_ft; }
    const int fd() const { return m_ft.fd; }
    const bool is_http() const { return m_is_http; }

    MsgHead* msg_head() { return m_msg_head; }
    MsgBody* msg_body() { return m_msg_body; }
    HttpMsg* http_msg() { return m_http_msg; }
    const MsgHead* msg_head() const { return m_msg_head; }
    const MsgBody* msg_body() const { return m_msg_body; }
    const HttpMsg* http_msg() const { return m_http_msg; }

   private:
    fd_t m_ft;
    bool m_is_http = false;
    MsgHead* m_msg_head = nullptr;  // protobuf msg head.
    MsgBody* m_msg_body = nullptr;  // protobuf msg body.
    HttpMsg* m_http_msg = nullptr;  // http msg.
};

};  // namespace kim
