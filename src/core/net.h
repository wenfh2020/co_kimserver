#ifndef __KIM_NET_H__
#define __KIM_NET_H__

#include "protobuf/proto/http.pb.h"
#include "protobuf/proto/msg.pb.h"
#include "server.h"
#include "util/json/CJsonObject.hpp"
#include "util/util.h"

namespace kim {

class INet {
   public:
    INet() {}
    virtual ~INet() {}

    virtual double now() { return time_now(); }
    virtual uint64_t new_seq() { return 0; }
    virtual CJsonObject& config() { return m_config; }

    virtual bool send_ack(
        const fd_t& fdata, const MsgHead& head, const MsgBody& body,
        int err, const std::string& errstr = "", const std::string& data = "") { return false; }

   protected:
    CJsonObject m_config;
};

}  // namespace kim

#endif  //__KIM_NET_H__
