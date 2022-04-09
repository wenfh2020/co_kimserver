#pragma once

#include "../server.h"
#include "codec.h"

namespace kim {

class CodecProto : public Codec {
   public:
    CodecProto(Log* logger, Codec::TYPE codec);
    virtual ~CodecProto() {}

    virtual Codec::STATUS encode(const MsgHead& head, const MsgBody& body, SocketBuffer* sbuf) override;
    virtual Codec::STATUS decode(SocketBuffer* sbuf, MsgHead& head, MsgBody& body) override;
};

}  // namespace kim
