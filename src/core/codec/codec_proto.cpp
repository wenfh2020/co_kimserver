#include "codec_proto.h"

#define PROTO_MSG_HEAD_LEN 15

namespace kim {

CodecProto::CodecProto(std::shared_ptr<Log> logger, Codec::TYPE codec)
    : Codec(logger, codec) {
}

Codec::STATUS CodecProto::encode(std::shared_ptr<Msg> msg, SocketBuffer* sbuf) {
    if (sbuf == nullptr || msg == nullptr) {
        LOG_ERROR("invalid param!");
        return CodecProto::STATUS::ERR;
    }

    auto head = msg->head();
    auto body = msg->body();
    auto body_len = body->ByteSizeLong();

    auto write_len = sbuf->_write(head->SerializeAsString().c_str(), PROTO_MSG_HEAD_LEN);
    if (write_len != PROTO_MSG_HEAD_LEN) {
        LOG_ERROR("encode head failed!, cmd: %d, seq: %d",
                  head->cmd(), head->seq());
        return CodecProto::STATUS::ERR;
    }

    auto len = write_len;

    if (head->len() <= 0) {
        // msg maybe has empty body, like heartbeat.
        return CodecProto::STATUS::OK;
    }

    write_len = sbuf->_write(body->SerializeAsString().c_str(), body_len);
    if (write_len != (int)body_len) {
        LOG_ERROR("encode failed! cmd: %d, seq: %d, write len: %d, body len: %lu",
                  head->cmd(), head->seq(), write_len, body_len);
        sbuf->set_write_index(sbuf->write_index() - len);
        return CodecProto::STATUS::ERR;
    }

    return CodecProto::STATUS::OK;
}

Codec::STATUS CodecProto::decode(SocketBuffer* sbuf, std::shared_ptr<Msg> msg) {
    if (sbuf == nullptr || msg == nullptr) {
        LOG_ERROR("invalid param");
        return CodecProto::STATUS::ERR;
    }

    auto head = msg->head();
    auto body = msg->body();

    // LOG_TRACE("decode data len: %d, cur read index: %d, write index: %d",
    //           sbuf->readable_len(), sbuf->read_index(), sbuf->write_index());

    if (sbuf->readable_len() < PROTO_MSG_HEAD_LEN) {
        // LOG_TRACE("wait for enough data to decode.");
        return CodecProto::STATUS::PAUSE;
    }

    // parse msg head.
    bool ret = head->ParseFromArray(sbuf->raw_read_buffer(), PROTO_MSG_HEAD_LEN);
    if (!ret) {
        LOG_ERROR("decode  head failed! data len: %d, cur read index: %d, write index: %d",
                  sbuf->readable_len(), sbuf->read_index(), sbuf->write_index());
        return CodecProto::STATUS::ERR;
    }

    // msg body maybe empty, like heartbeat.
    if (head->len() <= 0) {
        sbuf->skip_bytes(PROTO_MSG_HEAD_LEN);
        return CodecProto::STATUS::OK;
    }

    // parse msg body.
    if ((int)sbuf->readable_len() < PROTO_MSG_HEAD_LEN + head->len()) {
        LOG_TRACE("wait for enough data to decode msg body.");
        // wait for more data to decode.
        return CodecProto::STATUS::PAUSE;
    }

    ret = body->ParseFromArray(sbuf->raw_read_buffer() + PROTO_MSG_HEAD_LEN, head->len());
    if (!ret) {
        LOG_ERROR("cmd: %d, seq: %d, parse msg body failed!",
                  head->cmd(), head->seq());
        return CodecProto::STATUS::ERR;
    }

    sbuf->skip_bytes(PROTO_MSG_HEAD_LEN + head->len());
    // LOG_TRACE("sbuf left readable len: %d", sbuf->readable_len());
    return CodecProto::STATUS::OK;
}

}  // namespace kim