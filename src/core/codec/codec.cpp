#include "codec.h"

#include <cryptopp/gzip.h>

#if defined(CRYPTOPP_NO_GLOBAL_BYTE)
using CryptoPP::byte;
#endif

namespace kim {

bool Codec::set_codec(Codec::TYPE codec) {
    if (codec < Codec::TYPE::UNKNOWN || codec >= Codec::TYPE::COUNT) {
        return false;
    }
    m_codec = codec;
    return true;
}

Codec::TYPE Codec::get_codec_type(const std::string& codec_type) {
    if (strcasecmp(codec_type.c_str(), "protobuf") == 0) {
        return Codec::TYPE::PROTOBUF;
    } else if (strcasecmp(codec_type.c_str(), "http") == 0) {
        return Codec::TYPE::HTTP;
    } else if (strcasecmp(codec_type.c_str(), "private") == 0) {
        return Codec::TYPE::PRIVATE;
    } else {
        return Codec::TYPE::UNKNOWN;
    }
}

Codec::STATUS Codec::encode(const MsgHead& head, const MsgBody& body, SocketBuffer* sbuf) {
    LOG_TRACE("encode");
    return Codec::STATUS::ERR;
}

Codec::STATUS Codec::decode(SocketBuffer* sbuf, MsgHead& head, MsgBody& body) {
    LOG_TRACE("decode");
    return Codec::STATUS::ERR;
}

bool Codec::gzip(const std::string& src, std::string& dst) {
    try {
        CryptoPP::Gzip zip;
        zip.Put((byte*)src.c_str(), src.size());
        zip.MessageEnd();

        CryptoPP::word64 avail = zip.MaxRetrievable();
        if (avail) {
            dst.resize(avail);
            zip.Get((byte*)&dst[0], dst.size());
        }
    } catch (CryptoPP::InvalidDataFormat& e) {
        LOG_ERROR("%s", e.GetWhat().c_str());
        return false;
    }
    return true;
}

bool Codec::ungzip(const std::string& src, std::string& dst) {
    try {
        CryptoPP::Gunzip zip;
        zip.Put((byte*)src.c_str(), src.size());
        zip.MessageEnd();
        CryptoPP::word64 avail = zip.MaxRetrievable();
        if (avail) {
            dst.resize(avail);
            zip.Get((byte*)&dst[0], dst.size());
        }
    } catch (CryptoPP::InvalidDataFormat& e) {
        LOG_ERROR("%s", e.GetWhat().c_str());
        return false;
    }
    return (true);
}

};  // namespace kim
