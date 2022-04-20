#pragma once

#include "../server.h"
#include "codec.h"

namespace kim {

class CodecProto : public Codec {
   public:
    CodecProto(std::shared_ptr<Log> logger, Codec::TYPE codec);
    virtual ~CodecProto() {}

    virtual Codec::STATUS decode(SocketBuffer* sbuf, std::shared_ptr<Msg> msg) override;
    virtual Codec::STATUS encode(std::shared_ptr<Msg> msg, SocketBuffer* sbuf) override;
};

}  // namespace kim
