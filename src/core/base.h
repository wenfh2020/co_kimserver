#ifndef __KIM_BASE_H__
#define __KIM_BASE_H__

#include "net.h"
#include "util/log.h"

namespace kim {

class Base {
   public:
    Base() {}
    Base(Log* logger, INet* net, uint64_t id = 0, const std::string& name = "")
        : m_logger(logger), m_net(net), m_id(id), m_name(name) {
    }
    Base(const Base&) = delete;
    Base& operator=(const Base&) = delete;
    virtual ~Base() {}

   public:
    void set_id(uint64_t id) { m_id = id; }
    uint64_t id() { return m_id; }

    Log* logger() { return m_logger; }
    void set_logger(Log* logger) { m_logger = logger; }

    INet* net() { return m_net; }
    void set_net(INet* net) { m_net = net; }

    void set_name(const std::string& name) { m_name = name; }
    const std::string& name() const { return m_name; }
    const char* name() { return m_name.c_str(); }

   protected:
    Log* m_logger = nullptr;
    INet* m_net = nullptr;
    uint64_t m_id = 0;
    std::string m_name;
};

}  // namespace kim

#endif  //__KIM_BASE_H__
