#ifndef __CODEC_HTTP_H__
#define __CODEC_HTTP_H__

#include <unordered_map>

#include "../server.h"
#include "./util/http/http_parser.h"
#include "codec.h"

namespace kim {

class CodecHttp : public Codec {
   public:
    CodecHttp(Log *logger, Codec::TYPE type, double keep_alive = -1.0);
    virtual ~CodecHttp();

    Codec::STATUS encode(const HttpMsg &msg, SocketBuffer *sbuf);
    Codec::STATUS decode(SocketBuffer *sbuf, HttpMsg &msg);

    Codec::STATUS encode(const MsgHead &head, const MsgBody &body, SocketBuffer *sbuf);
    Codec::STATUS decode(SocketBuffer *sbuf, MsgHead &head, MsgBody &body);

    std::string to_string(const HttpMsg &oHttpMsg);
    static void decode_params(const std::string &s, std::map<std::string, std::string> &params);

    int keep_alive() { return m_keep_alive; }

   protected:
    static int on_message_begin(http_parser *parser);
    static int on_url(http_parser *parser, const char *at, size_t len);
    static int on_status(http_parser *parser, const char *at, size_t len);
    static int on_header_field(http_parser *parser, const char *at, size_t len);
    static int on_header_value(http_parser *parser, const char *at, size_t len);
    static int on_headers_complete(http_parser *parser);
    static int on_body(http_parser *parser, const char *at, size_t len);
    static int on_message_complete(http_parser *parser);
    static int on_chunk_header(http_parser *parser);
    static int on_chunk_complete(http_parser *parser);

   private:
    int m_http_major = 1;
    int m_http_minor = 1;
    int m_encode_cnt = 0;
    int m_decode_cnt = 0;
    bool m_is_client = false;
    double m_keep_alive = -1.0;

    http_parser m_parser;
    http_parser_settings m_parser_setting;
    std::unordered_map<std::string, std::string> m_http_headers;
};

};  // namespace kim

#endif  //__CODEC_HTTP_H__