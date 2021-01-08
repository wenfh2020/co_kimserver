#ifndef __KIM_BASE_H__
#define __KIM_BASE_H__

#include "util/log.h"

namespace kim {

class Base {
   public:
    Base() {}
    Base(uint64_t id, Log* logger, const std::string& name = "")
        : m_id(id), m_logger(logger), m_name(name) {
    }
    Base(const Base&) = delete;
    Base& operator=(const Base&) = delete;
    virtual ~Base() {}

   public:
    void set_id(uint64_t id) { m_id = id; }
    uint64_t id() { return m_id; }

    void set_logger(Log* logger) { m_logger = logger; }

    void set_name(const std::string& name) { m_name = name; }
    const std::string& name() const { return m_name; }
    const char* name() { return m_name.c_str(); }

   protected:
    uint64_t m_id = 0;
    Log* m_logger = nullptr;
    std::string m_name;
};

}  // namespace kim

#endif  //__KIM_BASE_H__
