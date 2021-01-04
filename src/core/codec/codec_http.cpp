#include "codec_http.h"

#define STATUS_CODE(code, str) \
    case code:                 \
        return str;

static const char* status_string(int code) {
    switch (code) {
        STATUS_CODE(100, "Continue")
        STATUS_CODE(101, "Switching Protocols")
        STATUS_CODE(102, "Processing")  // RFC 2518) obsoleted by RFC 4918
        STATUS_CODE(200, "OK")
        STATUS_CODE(201, "Created")
        STATUS_CODE(202, "Accepted")
        STATUS_CODE(203, "Non-Authoritative Information")
        STATUS_CODE(204, "No Content")
        STATUS_CODE(205, "Reset Content")
        STATUS_CODE(206, "Partial Content")
        STATUS_CODE(207, "Multi-Status")  // RFC 4918
        STATUS_CODE(300, "Multiple Choices")
        STATUS_CODE(301, "Moved Permanently")
        STATUS_CODE(302, "Moved Temporarily")
        STATUS_CODE(303, "See Other")
        STATUS_CODE(304, "Not Modified")
        STATUS_CODE(305, "Use Proxy")
        STATUS_CODE(307, "Temporary Redirect")
        STATUS_CODE(400, "Bad Request")
        STATUS_CODE(401, "Unauthorized")
        STATUS_CODE(402, "Payment Required")
        STATUS_CODE(403, "Forbidden")
        STATUS_CODE(404, "Not Found")
        STATUS_CODE(405, "Method Not Allowed")
        STATUS_CODE(406, "Not Acceptable")
        STATUS_CODE(407, "Proxy Authentication Required")
        STATUS_CODE(408, "Request Time-out")
        STATUS_CODE(409, "Conflict")
        STATUS_CODE(410, "Gone")
        STATUS_CODE(411, "Length Required")
        STATUS_CODE(412, "Precondition Failed")
        STATUS_CODE(413, "Request Entity Too Large")
        STATUS_CODE(414, "Request-URI Too Large")
        STATUS_CODE(415, "Unsupported Media Type")
        STATUS_CODE(416, "Requested Range Not Satisfiable")
        STATUS_CODE(417, "Expectation Failed")
        STATUS_CODE(418, "I\"m a teapot")         // RFC 2324
        STATUS_CODE(422, "Unprocessable Entity")  // RFC 4918
        STATUS_CODE(423, "Locked")                // RFC 4918
        STATUS_CODE(424, "Failed Dependency")     // RFC 4918
        STATUS_CODE(425, "Unordered Collection")  // RFC 4918
        STATUS_CODE(426, "Upgrade Required")      // RFC 2817
        STATUS_CODE(500, "Internal Server Error")
        STATUS_CODE(501, "Not Implemented")
        STATUS_CODE(502, "Bad Gateway")
        STATUS_CODE(503, "Service Unavailable")
        STATUS_CODE(504, "Gateway Time-out")
        STATUS_CODE(505, "HTTP Version not supported")
        STATUS_CODE(506, "Variant Also Negotiates")  // RFC 2295
        STATUS_CODE(507, "Insufficient Storage")     // RFC 4918
        STATUS_CODE(509, "Bandwidth Limit Exceeded")
        STATUS_CODE(510, "Not Extended")  // RFC 2774
    }

    return 0;
}

namespace kim {

#define CHECK_WRITE(x)             \
    if ((x) < 0) {                 \
        LOG_ERROR("write error!"); \
        goto error;                \
    }

CodecHttp::CodecHttp(Log* logger, Codec::TYPE type, double keep_alive)
    : Codec(logger, type), m_keep_alive(keep_alive) {
}

CodecHttp::~CodecHttp() {
}

Codec::STATUS CodecHttp::encode(const MsgHead& head, const MsgBody& body, SocketBuffer* sbuf) {
    return Codec::STATUS::ERR;
}

Codec::STATUS CodecHttp::decode(SocketBuffer* sbuf, MsgHead& head, MsgBody& body) {
    return Codec::STATUS::ERR;
}

Codec::STATUS CodecHttp::encode(const HttpMsg& msg, SocketBuffer* sbuf) {
    LOG_TRACE("readable len: %d, read index: %d, write index: %d",
              sbuf->readable_len(), sbuf->read_index(), sbuf->write_index());

    if (++m_encode_cnt > m_decode_cnt) {
        m_is_client = true;
    }

    if (msg.http_major() == 0) {
        LOG_WARN("miss http version!");
        m_http_headers.clear();
        return Codec::STATUS::ERR;
    }

    int size = 0, writed_len = 0, write_index = 0;
    bool is_chunked = false, is_gzip = false;

    if (msg.type() == HTTP_REQUEST) {
        if (msg.url().empty()) {
            LOG_WARN("miss url!");
            m_http_headers.clear();
            return Codec::STATUS::ERR;
        }

        int port = 0;
        std::string host, path, schema;
        struct http_parser_url parser_url;

        if (http_parser_parse_url(
                msg.url().c_str(), msg.url().length(), 0, &parser_url) != 0) {
            LOG_WARN("http_parser_parse_url error!");
            goto error;
        }

        port = (parser_url.field_set & (1 << UF_PORT)) ? parser_url.port : 80;

        if (parser_url.field_set & (1 << UF_HOST)) {
            host = msg.url().substr(parser_url.field_data[UF_HOST].off,
                                    parser_url.field_data[UF_HOST].len);
        }

        if (parser_url.field_set & (1 << UF_PATH)) {
            host = msg.url().substr(parser_url.field_data[UF_PATH].off,
                                    parser_url.field_data[UF_PATH].len);
        }

        schema = msg.url().substr(0, msg.url().find_first_of(':'));
        if (schema == "https") {
            port = 443;
        }

        if (parser_url.field_data[UF_PATH].off >= msg.url().size()) {
            LOG_WARN("invalid url \"%s\"!", msg.url().c_str());
            goto error;
        }

        size = sbuf->_printf(
            "%s %s HTTP/%u.%u\r\n",
            http_method_str((http_method)msg.method()),
            msg.url().substr(parser_url.field_data[UF_PATH].off, std::string::npos).c_str(),
            msg.http_major(), msg.http_minor());
        CHECK_WRITE(size);
        writed_len += size;

        CHECK_WRITE(size = sbuf->_printf("Host: %s:%d\r\n", host.c_str(), port));
        writed_len += size;
    } else if (msg.type() == HTTP_RESPONSE) {
        if (msg.status_code() == 0) {
            LOG_WARN("miss status code!");
            goto error;
        }

        size = sbuf->_printf("HTTP/%u.%u %u %s\r\n",
                             m_http_major, m_http_minor,
                             msg.status_code(), status_string(msg.status_code()));
        CHECK_WRITE(size);
        writed_len += size;

        if (!m_is_client) {
            m_http_headers["Connection"] = (m_keep_alive == 0) ? "close" : "keep-alive";
        }
        m_http_headers["Allow"] = "POST,GET";
        m_http_headers["Server"] = "KimHttp";
    }

    for (const auto& it : msg.headers()) {
        if (it.first == "Host" || it.first == "Content-Length") {
            continue;
        }
        if (m_http_headers.find(it.first) != m_http_headers.end()) {
            m_http_headers[it.first] = it.second;
        }
    }

    for (const auto& it : m_http_headers) {
        CHECK_WRITE(size = sbuf->_printf("%s: %s\r\n", it.first.c_str(), it.second.c_str()));
        writed_len += size;

        if (it.first == "Content-Encoding" && it.second == "gzip") {
            is_gzip = true;
        }

        if (it.first == "Transfer-Encoding" && it.second == "chunked") {
            is_chunked = true;
        }
    }

    if (msg.body().size() > 0) {
        std::string gzip_data;
        if (is_gzip) {
            if (gzip(msg.body(), gzip_data)) {
                LOG_WARN("gzip error!");
                goto error;
            }
        }

        if (is_chunked) {
            // Transfer-Encoding: chunked
            if (msg.encoding() == 0) {
                CHECK_WRITE(size = sbuf->_printf("\r\n"));
                writed_len += size;
            } else {
                sbuf->set_write_index(sbuf->write_index() - writed_len);
                writed_len = 0;
            }

            std::string data = (gzip_data.size() > 0) ? gzip_data : msg.body();
            if (data.size() > 0) {
                CHECK_WRITE(size = sbuf->_printf("%x\r\n", data.size()));
                writed_len += size;
                CHECK_WRITE(size = sbuf->_write(data.c_str(), data.size()));
                writed_len += size;
            }

            CHECK_WRITE(size = sbuf->_printf("\r\n0\r\n\r\n"));
            writed_len += size;
        } else {
            // Content-Length: %u
            std::string data = (gzip_data.size() > 0) ? gzip_data : msg.body();
            CHECK_WRITE(size = sbuf->_printf("Content-Length: %u\r\n\r\n", data.size()));
            writed_len += size;
            CHECK_WRITE(size = sbuf->_write(data.c_str(), data.size()));
            writed_len += size;
        }
    } else {
        if (is_chunked) {
            if (msg.encoding() == 0) {
                CHECK_WRITE(size = sbuf->_printf("\r\n"));
                writed_len += size;
            } else {
                sbuf->set_write_index(sbuf->write_index() - writed_len);
            }

            CHECK_WRITE(size = sbuf->_printf("0\r\n\r\n"));
            writed_len += size;
        } else {
            CHECK_WRITE(size = sbuf->_printf("Content-Length: 0\r\n\r\n"));
            writed_len += size;
        }
    }

    m_http_headers.clear();
    CHECK_WRITE(size = sbuf->write_byte('\0'));
    write_index = sbuf->write_index();
    sbuf->set_write_index(write_index - size);

    // LOG_DEBUG("%s", sbuf->raw_write_buffer());
    LOG_TRACE("readable len: %d, read index: %d, write index: %d, writed len: %d",
              sbuf->readable_len(), sbuf->read_index(),
              sbuf->write_index(), writed_len);
    LOG_TRACE("\n%s", to_string(msg).c_str());
    return Codec::STATUS::OK;

error:
    sbuf->set_write_index(sbuf->write_index() - writed_len);
    m_http_headers.clear();
    return Codec::STATUS::ERR;
}

Codec::STATUS CodecHttp::decode(SocketBuffer* sbuf, HttpMsg& msg) {
    if (!sbuf->is_readable()) {
        return Codec::STATUS::PAUSE;  // not enough data to decode.
    }

    ++m_decode_cnt;
    m_parser_setting.on_message_begin = on_message_begin;
    m_parser_setting.on_url = on_url;
    m_parser_setting.on_status = on_status;
    m_parser_setting.on_header_field = on_header_field;
    m_parser_setting.on_header_value = on_header_value;
    m_parser_setting.on_headers_complete = on_headers_complete;
    m_parser_setting.on_body = on_body;
    m_parser_setting.on_message_complete = on_message_complete;
    m_parser_setting.on_chunk_header = on_chunk_header;
    m_parser_setting.on_chunk_complete = on_chunk_complete;
    m_parser.data = &msg;
    http_parser_init(&m_parser, HTTP_BOTH);

    const char* buffer = sbuf->raw_read_buffer();
    size_t buf_len = sbuf->readable_len();
    size_t len = http_parser_execute(&m_parser, &m_parser_setting, buffer, buf_len);

    if (msg.is_decoding()) {
        return Codec::STATUS::PAUSE;
    }

    if (m_parser.http_errno != HPE_OK) {
        LOG_WARN("parse http message failed! error: %s",
                 http_errno_name((http_errno)m_parser.http_errno));
        return Codec::STATUS::ERR;
    }

    sbuf->advance_read_index(len);

    if (msg.type() == HTTP_REQUEST) {
        m_http_major = msg.http_major();
        m_http_minor = msg.http_minor();
        m_keep_alive = (msg.keep_alive() >= 0) ? msg.keep_alive() : m_keep_alive;
    }

    auto iter = msg.headers().find("Content-Encoding");
    if (iter != msg.headers().end()) {
        if ("gzip" == iter->second) {
            std::string strData;
            if (ungzip(msg.body(), strData)) {
                msg.set_body(strData);
            } else {
                LOG_WARN("guzip error!");
                return Codec::STATUS::ERR;
            }
        }
    }

    LOG_TRACE("\n%s", to_string(msg).c_str());
    return Codec::STATUS::OK;
}

int CodecHttp::on_message_begin(http_parser* parser) {
    HttpMsg* msg = (HttpMsg*)parser->data;
    msg->set_is_decoding(true);
    return 0;
}

int CodecHttp::on_url(http_parser* parser, const char* at, size_t len) {
    HttpMsg* msg = (HttpMsg*)parser->data;
    msg->set_url(at, len);
    struct http_parser_url parser_url;

    if (http_parser_parse_url(at, len, 0, &parser_url) == 0) {
        if (parser_url.field_set & (1 << UF_PATH)) {
            char* path = (char*)malloc(parser_url.field_data[UF_PATH].len);
            strncpy(path, at + parser_url.field_data[UF_PATH].off,
                    parser_url.field_data[UF_PATH].len);
            msg->set_path(path, parser_url.field_data[UF_PATH].len);
            free(path);
        }

        if (parser_url.field_set & (1 << UF_QUERY)) {
            std::string query;
            query.assign(at + parser_url.field_data[UF_QUERY].off,
                         parser_url.field_data[UF_QUERY].len);
            std::map<std::string, std::string> params;
            decode_params(query, params);
            for (auto it = params.begin(); it != params.end(); ++it) {
                (*msg->mutable_params())[it->first] = it->second;
            }
        }
    }

    return 0;
}

int CodecHttp::on_status(http_parser* parser, const char* at, size_t len) {
    HttpMsg* msg = (HttpMsg*)parser->data;
    msg->set_status_code(parser->status_code);
    return (0);
}

int CodecHttp::on_header_field(http_parser* parser, const char* at, size_t len) {
    HttpMsg* msg = (HttpMsg*)parser->data;
    msg->set_body(at, len);  // firstly header and then on_header_value get header's value.
    return (0);
}

int CodecHttp::on_header_value(http_parser* parser, const char* at, size_t len) {
    HttpMsg* msg = (HttpMsg*)parser->data;
    std::string header = msg->body();
    std::string value(at, len);

    msg->set_body("");
    (*msg->mutable_headers())[header] = value;

    if (header == "Keep-Alive") {
        msg->set_keep_alive(atof(value.c_str()));
    } else if (header == "Connection") {
        if (value == "keep-alive") {
            msg->set_keep_alive(-1.0);  // timer logic will close it.
        } else if (value == "close") {
            msg->set_keep_alive(0.0);
        }
    }

    return 0;
}

int CodecHttp::on_body(http_parser* parser, const char* at, size_t len) {
    HttpMsg* msg = (HttpMsg*)parser->data;
    if (msg->body().size() > 0) {
        msg->mutable_body()->append(at, len);
    } else {
        msg->set_body(at, len);
    }
    return (0);
}

int CodecHttp::on_headers_complete(http_parser* parser) {
    return 0;
}

int CodecHttp::on_chunk_header(http_parser* parser) {
    return (0);
}

int CodecHttp::on_chunk_complete(http_parser* parser) {
    return (0);
}

int CodecHttp::on_message_complete(http_parser* parser) {
    HttpMsg* msg = (HttpMsg*)parser->data;
    if (parser->status_code != 0) {
        msg->set_status_code(parser->status_code);
        msg->set_type(HTTP_RESPONSE);
    } else {
        msg->set_method(parser->method);
        msg->set_type(HTTP_REQUEST);
    }
    msg->set_http_major(parser->http_major);
    msg->set_http_minor(parser->http_minor);
    msg->set_is_decoding(false);

    if (!http_should_keep_alive(parser)) {
        msg->set_keep_alive(0.0);
    }

    return 0;
}

std::string CodecHttp::to_string(const HttpMsg& msg) {
    std::string data;
    char prover[32];
    sprintf(prover, "HTTP/%u.%u", msg.http_major(), msg.http_minor());

    if (msg.type() == HTTP_REQUEST) {
        data += http_method_str((http_method)msg.method());
        data += " ";
        data += msg.url();
        data += " ";
        data += prover;
        data += "\r\n";
    } else {
        data += prover;
        if (msg.status_code() > 0) {
            data += "\r\n";
            data += std::to_string(msg.status_code());
            data += "\r\n";
        }
    }

    for (const auto& it : msg.headers()) {
        data += it.first;
        data += ":";
        data += it.second;
        data += "\r\n";
    }
    data += "\r\n";

    if (msg.body().size() > 0) {
        data += msg.body();
        data += "\r\n\r\n";
    }

    return data;
}

void CodecHttp::decode_params(const std::string& s, std::map<std::string, std::string>& params) {
    params.clear();
    bool is_value = false;
    std::string key, value;

    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '&') {
            if (key.size() > 0) {
                params[key] = value;
            }
            key.clear();
            value.clear();
            is_value = false;
        } else if (s[i] == '=') {
            is_value = true;
        } else {
            if (is_value) {
                value += s[i];
            } else {
                key += s[i];
            }
        }
    }

    if (key.size() > 0) {
        params[key] = value;
    }
}

};  // namespace kim