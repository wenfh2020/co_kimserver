#ifndef __SOCKET_BUFFER_H__
#define __SOCKET_BUFFER_H__

#include <stdarg.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <sys/uio.h>
#include <sys/unistd.h>

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

/**
 *
 *       +-------------------+------------------+------------------+
 *       | readed bytes      |  readable bytes  |  writable bytes  |
 *       +-------------------+------------------+------------------+
 *       |                   |                  |                  |
 *   m_buffer    <=      m_read_idx   <=   m_write_idx    <=   m_buffer_len
 *
 */

namespace kim {

typedef unsigned int uint32_t;

class SocketBuffer {
   private:
    char* m_buffer = nullptr;  // total allocation available in the buffer field.
    size_t m_buffer_len = 0;   // raw buffer length
    size_t m_write_idx = 0;    // current write index.
    size_t m_read_idx = 0;     // current read index.

   public:
    static const size_t BUFFER_MAX_READ = 8192;
    static const size_t DEFAULT_BUFFER_SIZE = 32;

    inline SocketBuffer() {}
    inline SocketBuffer(size_t size) { ensure_writeable(size); }
    inline ~SocketBuffer() {
        if (m_buffer != nullptr) {
            free(m_buffer);
            m_buffer = nullptr;
        }
    }
    inline size_t read_index() { return m_read_idx; }
    inline size_t write_index() { return m_write_idx; }
    inline void set_read_index(size_t idx) { m_read_idx = idx; }
    inline void advance_read_index(int step) { m_read_idx += step; }
    inline void set_write_index(size_t idx) { m_write_idx = idx; }
    inline void advance_write_index(int step) { m_write_idx += step; }
    inline bool is_readable() { return m_write_idx > m_read_idx; }
    inline bool is_writeable() { return m_buffer_len > m_write_idx; }
    inline size_t readable_len() { return is_readable() ? m_write_idx - m_read_idx : 0; }
    inline size_t writeable_len() { return is_writeable() ? m_buffer_len - m_write_idx : 0; }

    // recovery writebale buffer and alreay readed buffer.
    inline size_t compact(size_t size) {
        if (writeable_len() < size) {
            return 0;
        }

        uint32_t total = capacity();
        uint32_t readable_bytes = readable_len();

        char* new_buffer = nullptr;
        if (readable_bytes > 0) {
            new_buffer = (char*)malloc(readable_bytes);
            if (new_buffer == nullptr) {
                return 0;
            }
            memcpy(new_buffer, m_buffer + m_read_idx, readable_bytes);
        }

        if (m_buffer != nullptr) {
            free(m_buffer);
            m_buffer = nullptr;
        }

        m_buffer = new_buffer;
        m_read_idx = 0;
        m_write_idx = readable_bytes;
        m_buffer_len = readable_bytes;
        return (total - readable_bytes);
    }

    inline bool ensure_writeable(size_t min) {
        if (writeable_len() >= min) {
            // enough space to write.
            return true;
        }

        // not enough space to write, then alloc more.
        size_t cap = capacity();
        if (cap > BUFFER_MAX_READ) {
            compact(min);
        }

        if (cap == 0) {
            cap = DEFAULT_BUFFER_SIZE;
        }

        size_t min_cap = write_index() + min;
        while (cap < min_cap) {
            cap <<= 1;
        }

        char* tmp = (char*)malloc(cap);
        if (tmp != nullptr) {
            memcpy(tmp, m_buffer + m_read_idx, readable_len());
            free(m_buffer);
            m_buffer = tmp;
            m_buffer_len = cap;
            m_write_idx = readable_len();
            m_read_idx = 0;
            return true;
        }

        return false;
    }

    inline char* raw_write_buffer() { return m_buffer + m_write_idx; }
    inline const char* raw_write_buffer() const { return m_buffer + m_read_idx; }
    inline const char* raw_read_buffer() const { return m_buffer + m_read_idx; }
    inline size_t capacity() const { return m_buffer_len; }
    inline void limit() { m_buffer_len = m_write_idx; }
    inline void clear() { m_write_idx = m_read_idx = 0; }
    inline int _read(void* data_out, size_t len) {
        if (len > readable_len()) {
            return -1;
        }
        memcpy(data_out, m_buffer + m_read_idx, len);
        m_read_idx += len;
        return len;
    }

    inline int _write(const void* data_in, size_t len) {
        if (!ensure_writeable(len)) {
            return -1;
        }
        memcpy(m_buffer + m_write_idx, data_in, len);
        m_write_idx += len;
        return len;
    }

    inline int _write(SocketBuffer* unit, size_t len) {
        if (unit == nullptr) {
            return -1;
        }
        if (len > unit->readable_len()) {
            len = unit->readable_len();
        }
        int ret = _write(unit->m_buffer + unit->m_read_idx, len);
        if (ret > 0) {
            unit->m_read_idx += ret;
        }
        return ret;
    }

    inline int write_byte(char ch) { return _write(&ch, 1); }

    inline int _read(SocketBuffer* unit, size_t len) {
        if (unit == nullptr) {
            return -1;
        }
        return unit->_write(this, len);
    }

    inline int set_bytes(void* data, size_t len, size_t index) {
        if (data == nullptr) {
            return -1;
        }
        if (index + len > m_write_idx) {
            return -1;
        }
        memcpy(m_buffer + index, data, len);
        return len;
    }

    inline bool read_byte(char& ch) { return _read(&ch, 1) == 1; }

    inline int copy_out(void* data_out, size_t len) {
        if (len == 0) {
            return 0;
        }
        if (len > readable_len()) {
            len = readable_len();
        }
        memcpy(data_out, m_buffer + m_read_idx, len);
        return len;
    }

    inline int copy_out(SocketBuffer* unit, size_t len) {
        if (unit == nullptr || !unit->ensure_writeable(len)) {
            return -1;
        }
        int ret = copy_out(unit->m_buffer + +unit->m_write_idx, len);
        if (ret > 0) {
            unit->m_write_idx += ret;
        }
        return ret;
    }

    inline void skip_bytes(size_t len) { advance_read_index(len); }

    inline void DiscardReadedBytes() {
        if (m_read_idx > 0) {
            if (is_readable()) {
                size_t tmp = readable_len();
                memmove(m_buffer, m_buffer + m_read_idx, tmp);
                m_read_idx = 0;
                m_write_idx = tmp;
            } else {
                m_read_idx = m_write_idx = 0;
            }
        }
    }

    int _printf(const char* fmt, ...);
    int _vprintf(const char* fmt, va_list ap);
    int read_fd(int fd, int& err);
    int write_fd(int fd, int& err);

    inline std::string ToString() { return std::string(m_buffer + m_read_idx, readable_len()); }
};

}  // namespace kim

#endif /* __SOCKET_BUFFER_H__ */
