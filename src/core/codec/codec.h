#pragma once

#include "../protobuf/proto/http.pb.h"
#include "../protobuf/proto/msg.pb.h"
#include "../server.h"
#include "../util/log.h"
#include "../util/socket_buffer.h"

namespace kim {

class Codec : public Logger {
   public:
    enum class TYPE {
        UNKNOWN = 0,
        PROTOBUF = 1,
        HTTP = 2,
        PRIVATE = 3,
        COUNT = 4,
    };

    enum class STATUS {
        OK = 0,
        ERR = 1,
        PAUSE = 2,
        CLOSED = 3,
    };

    Codec(std::shared_ptr<Log> logger, Codec::TYPE codec) : Logger(logger), m_codec(codec) {}
    virtual ~Codec() {}

    Codec(const Codec&) = delete;
    Codec& operator=(const Codec&) = delete;

    virtual Codec::STATUS decode(SocketBuffer* sbuf, MsgHead& head, MsgBody& body);
    virtual Codec::STATUS encode(const MsgHead& head, const MsgBody& body, SocketBuffer* sbuf);

    bool set_codec(Codec::TYPE codec);
    Codec::TYPE codec() { return m_codec; }
    static Codec::TYPE get_codec_type(const std::string& codec_type);

    bool gzip(const std::string& src, std::string& dst);
    bool ungzip(const std::string& src, std::string& dst);

   protected:
    Codec::TYPE m_codec = Codec::TYPE::PROTOBUF;
};

};  // namespace kim
