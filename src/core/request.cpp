#include "request.h"

namespace kim {

Request::Request(const fd_t& ft, bool is_http) : m_ft(ft), m_is_http(is_http) {
    if (is_http) {
        CHECK_NEW(m_http_msg, HttpMsg);
    } else {
        CHECK_NEW(m_msg_head, MsgHead);
        CHECK_NEW(m_msg_body, MsgBody);
    }
}

Request::~Request() {
    SAFE_DELETE(m_msg_head);
    SAFE_DELETE(m_msg_body);
    SAFE_DELETE(m_http_msg);
}

};  // namespace kim