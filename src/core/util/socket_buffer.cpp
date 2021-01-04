#include "socket_buffer.h"

#include <sys/socket.h>

namespace kim {

int SocketBuffer::_vprintf(const char* fmt, va_list ap) {
    char* buffer;
    size_t space;
    int sz, result = 0;
    va_list aq;

    /* make sure that at least some space is available */
    if (!ensure_writeable(64)) {
        goto done;
    }

    for (;;) {
        buffer = m_buffer + m_write_idx;
        space = writeable_len();

#ifndef va_copy
#define va_copy(dst, src) memcpy(&(dst), &(src), sizeof(va_list))
#endif
        va_copy(aq, ap);
        sz = vsnprintf(buffer, space, fmt, aq);
        va_end(aq);

        if (sz < 0) {
            goto done;
        }

        result += sz;
        if ((size_t)sz < space) {
            advance_write_index(sz);
            goto done;
        }

        if (!ensure_writeable(sz << 1)) {
            goto done;
        }
    }
    /* NOTREACHED */

done:
    return result;
}

int SocketBuffer::_printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = _vprintf(fmt, args);
    va_end(args);
    return ret;
}

int SocketBuffer::write_fd(int fd, int& err) {
    int n = -1;
    int readable = readable_len();
    if (readable == 0) {
        return 0;
    }

#if !defined(__APPLE__) && defined(HAVE_MSG_NOSIGNAL)
    n = ::send(fd, m_buffer + m_read_idx, readable, MSG_NOSIGNAL);
#else
    n = ::send(fd, m_buffer + m_read_idx, readable, 0);
#endif

    if (n < 0) {
        err = errno;
    } else {
        m_read_idx += n;
    }
    return n;
}

int SocketBuffer::read_fd(int fd, int& err) {
    char extrabuf[32768];
    struct iovec vec[2];
    size_t writable = writeable_len();

    vec[0].iov_base = m_buffer + m_write_idx;
    vec[0].iov_len = writable;
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);

    int n = readv(fd, vec, writable > sizeof(extrabuf) ? 1 : 2);
    if (n < 0) {
        err = errno;
    } else if ((size_t)n <= writable) {
        m_write_idx += n;
    } else {
        m_write_idx = m_buffer_len;
        _write(extrabuf, n - writable);
    }
    return n;
}

}  // namespace kim
