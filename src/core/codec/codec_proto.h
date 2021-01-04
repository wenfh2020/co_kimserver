#ifndef __KIM_CODEC_PROTO_H__
#define __KIM_CODEC_PROTO_H__

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

#endif  //__KIM_CODEC_PROTO_H__
