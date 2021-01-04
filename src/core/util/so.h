#ifndef __KIM_SO_H__
#define __KIM_SO_H__

#include <iostream>

namespace kim {

class So {
   public:
    So() {}
    virtual ~So() {}

    void set_so_handle(void* handle) { m_so_handle = handle; }
    void* so_handle() { return m_so_handle; }
    void set_so_path(const std::string& path) { m_so_path = path; }
    const std::string& so_path() const { return m_so_path; }
    const char* so_path() { return m_so_path.c_str(); }

   protected:
    std::string m_so_path;        // module so path.
    void* m_so_handle = nullptr;  // for dlopen ptr.
};

}  // namespace kim

#endif  //__KIM_SO_H__