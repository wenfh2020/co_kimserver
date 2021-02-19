#include "connection.h"

#include <unistd.h>

#include "codec/codec_http.h"
#include "codec/codec_proto.h"
#include "util/util.h"

namespace kim {

Connection::Connection(Log* logger, int fd, uint64_t id) : Logger(logger) {
    set_fd_data(fd, id);
    set_active_time(mstime());
}

Connection::~Connection() {
    SAFE_FREE(m_saddr);
    SAFE_DELETE(m_codec);
    SAFE_DELETE(m_recv_buf);
    SAFE_DELETE(m_send_buf);
}

bool Connection::init(Codec::TYPE codec) {
    LOG_TRACE("connection init fd: %d, codec type: %d", fd(), (int)codec);

    switch (codec) {
        case Codec::TYPE::HTTP: {
            m_codec = new CodecHttp(m_logger, codec);
            break;
        }
        case Codec::TYPE::PROTOBUF: {
            m_codec = new CodecProto(m_logger, codec);
            break;
        }
        default: {
            LOG_ERROR("invalid codec type: %d", (int)codec);
            break;
        }
    }

    if (m_codec == nullptr) {
        return false;
    }

    if (m_codec != nullptr) {
        set_active_time(mstime());
        m_codec->set_codec(codec);
    }

    return true;
}

bool Connection::is_http() {
    return (m_codec == nullptr) ? false : (m_codec->codec() == Codec::TYPE::HTTP);
}

Codec::STATUS Connection::conn_read() {
    if (is_invalid()) {
        LOG_ERROR("conn is closed! fd: %d, seq: %llu", fd(), id());
        return Codec::STATUS::ERR;
    }

    if ((CHECK_NEW(m_recv_buf, SocketBuffer)) == nullptr) {
        return Codec::STATUS::ERR;
    }

    int read_len = m_recv_buf->read_fd(fd(), m_errno);
    if (read_len >= 0) {
        LOG_TRACE("read from fd: %d, data len: %d, readed data len: %d",
                  fd(), read_len, m_recv_buf->readable_len());
    }

    if (read_len == 0) {
        LOG_TRACE("connection is closed! fd: %d!", fd());
        return Codec::STATUS::CLOSED;
    } else if (read_len < 0) {
        if (errno == EAGAIN) {
            return Codec::STATUS::PAUSE;
        } else {
            LOG_DEBUG("connection read error! fd: %d, err: %d, error: %s",
                      fd(), errno, strerror(errno));
            return Codec::STATUS::ERR;
        }
    } else {
        m_read_cnt++;
        m_read_bytes += read_len;

        /* recovery socket buffer. */
        if (m_recv_buf->capacity() > SocketBuffer::BUFFER_MAX_READ &&
            m_recv_buf->readable_len() < m_recv_buf->capacity() / 2) {
            m_recv_buf->compact(m_recv_buf->readable_len() * 2);
        }
        m_active_time = mstime();
    }

    return Codec::STATUS::OK;
}

Codec::STATUS Connection::conn_write() {
    if (is_invalid()) {
        LOG_ERROR("conn is invalid, fd: %d, %llu", fd(), id());
        return Codec::STATUS::ERR;
    }

    int write_len;
    SocketBuffer* sbuf;

    sbuf = m_send_buf;

    if (sbuf == nullptr || !sbuf->is_readable()) {
        LOG_TRACE("no data to send! fd: %d, seq: %llu", fd(), id());
        return Codec::STATUS::OK;
    }

    write_len = sbuf->write_fd(fd(), m_errno);
    if (write_len < 0) {
        if (m_errno == EAGAIN) {
            return Codec::STATUS::PAUSE;
        } else {
            LOG_WARN(
                "send data failed! error: %d, errstr: %s, "
                " fd: %d, seq: %llu, readable len: %d",
                m_errno, strerror(m_errno), fd(), id(), sbuf->readable_len());
            return Codec::STATUS::ERR;
        }
    }

    m_write_cnt++;
    m_write_bytes += write_len;

    LOG_TRACE("send to fd: %d, conn id: %llu, write len: %d, readed data len: %d",
              fd(), id(), write_len, sbuf->readable_len());

    /* recovery socket buffer. */
    if (sbuf->capacity() > SocketBuffer::BUFFER_MAX_READ &&
        sbuf->readable_len() < sbuf->capacity() / 2) {
        sbuf->compact(sbuf->readable_len() * 2);
    }

    m_active_time = mstime();
    return (sbuf->readable_len() > 0) ? Codec::STATUS::PAUSE : Codec::STATUS::OK;
}

Codec::STATUS Connection::conn_read(MsgHead& head, MsgBody& body) {
    Codec::STATUS ret = Codec::STATUS::PAUSE;

    if (m_recv_buf != nullptr) {
        ret = fetch_data(head, body);
    }

    if (ret == Codec::STATUS::PAUSE) {
        ret = conn_read();
    }

    if (ret != Codec::STATUS::OK) {
        return ret;
    }

    return decode_proto(head, body);
}

Codec::STATUS Connection::fetch_data(MsgHead& head, MsgBody& body) {
    /* continue to handle the data in buffer. */
    if (m_state == STATE::UNKOWN || m_state == STATE::ERROR) {
        LOG_ERROR("conn is invalid! fd: %d, seq: %llu", fd(), id());
        return Codec::STATUS::ERR;
    }
    return decode_proto(head, body);
}

Codec::STATUS Connection::decode_proto(MsgHead& head, MsgBody& body) {
    CodecProto* codec = dynamic_cast<CodecProto*>(m_codec);
    if (codec == nullptr) {
        return Codec::STATUS::ERR;
    }
    return codec->decode(m_recv_buf, head, body);
}

Codec::STATUS Connection::conn_append_message(const MsgHead& head, const MsgBody& body) {
    if (is_invalid()) {
        LOG_ERROR("conn is invalid! fd: %d, seq: %llu", fd(), id());
        return Codec::STATUS::ERR;
    }

    CodecProto* codec;
    Codec::STATUS status;

    codec = dynamic_cast<CodecProto*>(m_codec);
    if (codec == nullptr) {
        return Codec::STATUS::ERR;
    }

    if ((CHECK_NEW(m_send_buf, SocketBuffer)) == nullptr) {
        LOG_ERROR("alloc send buf failed!");
        return Codec::STATUS::ERR;
    }

    status = codec->encode(head, body, m_send_buf);
    if (status != Codec::STATUS::OK) {
        LOG_ERROR("encode packed failed! fd: %d, seq: %llu, status: %d",
                  fd(), id(), (int)status);
        return status;
    }

    return status;
}

Codec::STATUS Connection::conn_write(const MsgHead& head, const MsgBody& body) {
    Codec::STATUS status;

    status = conn_append_message(head, body);
    if (status != Codec::STATUS::OK) {
        LOG_ERROR("encode message failed!");
        return status;
    }

    return conn_write();
}

Codec::STATUS Connection::conn_write(
    const MsgHead& head, const MsgBody& body, SocketBuffer** buf, bool is_send) {
    if (is_invalid()) {
        LOG_ERROR("conn is invalid! fd: %d, seq: %llu", fd(), id());
        return Codec::STATUS::ERR;
    }

    CodecProto* codec = dynamic_cast<CodecProto*>(m_codec);
    if (codec == nullptr) {
        return Codec::STATUS::ERR;
    }

    if ((CHECK_NEW(*buf, SocketBuffer)) == nullptr) {
        LOG_ERROR("alloc send buf failed!");
        return Codec::STATUS::ERR;
    }

    Codec::STATUS status = codec->encode(head, body, *buf);
    if (status != Codec::STATUS::OK) {
        LOG_ERROR("encode packed failed! fd: %d, seq: %llu, status: %d",
                  fd(), id(), (int)status);
        return status;
    }

    return is_send ? conn_write() : status;
}

Codec::STATUS Connection::conn_read(HttpMsg& msg) {
    Codec::STATUS ret = conn_read();
    if (ret != Codec::STATUS::OK) {
        return ret;
    }
    return decode_http(msg);
}

Codec::STATUS Connection::fetch_data(HttpMsg& msg) {
    /* continue to handle the data in buffer. */
    if (m_state == STATE::UNKOWN || m_state == STATE::ERROR) {
        LOG_ERROR("conn is invalid! fd: %d, seq: %llu", fd(), id());
        return Codec::STATUS::ERR;
    }
    return decode_http(msg);
}

Codec::STATUS Connection::decode_http(HttpMsg& msg) {
    CodecHttp* codec = dynamic_cast<CodecHttp*>(m_codec);
    if (codec == nullptr) {
        return Codec::STATUS::ERR;
    }
    return codec->decode(m_recv_buf, msg);
}

Codec::STATUS Connection::conn_write(const HttpMsg& msg) {
    return conn_write(msg, &m_send_buf);
}

Codec::STATUS Connection::conn_write(const HttpMsg& msg, SocketBuffer** buf) {
    if (is_invalid()) {
        LOG_ERROR("conn is closed! fd: %d, seq: %llu", fd(), id());
        return Codec::STATUS::ERR;
    }

    CodecHttp* codec = dynamic_cast<CodecHttp*>(m_codec);
    if (codec == nullptr) {
        return Codec::STATUS::ERR;
    }

    if ((CHECK_NEW(*buf, SocketBuffer)) == nullptr) {
        LOG_ERROR("alloc send buf failed!");
        return Codec::STATUS::ERR;
    }

    Codec::STATUS status = codec->encode(msg, *buf);
    if (status != Codec::STATUS::OK) {
        LOG_ERROR("encode http packed failed! fd: %d, seq: %llu, status: %d",
                  fd(), id(), (int)status);
        return status;
    }

    return conn_write();
}

bool Connection::is_need_alive_check() {
    if (m_codec != nullptr) {
        if (m_codec->codec() == Codec::TYPE::HTTP) {
            return false;
        }
    }
    return true;
}

uint64_t Connection::keep_alive() {
    if (is_http()) {
        /* http codec has it's own keep alive. */
        CodecHttp* codec = dynamic_cast<CodecHttp*>(m_codec);
        if (codec != nullptr) {
            int keep_alive = codec->keep_alive();
            if (keep_alive >= 0) {
                return keep_alive;
            }
        }
    }
    return m_keep_alive;
}

void Connection::set_addr_info(struct sockaddr* saddr, size_t saddr_len) {
    SAFE_FREE(m_saddr);
    m_saddr = (struct sockaddr*)malloc(saddr_len);
    memcpy(m_saddr, saddr, saddr_len);
    m_saddr_len = saddr_len;
}

struct sockaddr* Connection::sockaddr() {
    return m_saddr;
}

}  // namespace kim