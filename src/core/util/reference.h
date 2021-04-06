#ifndef __KIM_REFERENCE_H__
#define __KIM_REFERENCE_H__

namespace kim {

class Reference {
   public:
    Reference() {}
    Reference(const Reference&) = delete;
    Reference& operator=(const Reference&) = delete;
    virtual ~Reference() {}

    void decr_ref() { --m_ref_cnt; }
    void incr_ref() { ++m_ref_cnt; }
    int ref_cnt() { return m_ref_cnt; }
    bool is_running() { return (m_ref_cnt > 0); }

   private:
    int m_ref_cnt = 0; /* number of session's owners. */
};

class SafeRef {
   public:
    SafeRef() = delete;
    SafeRef(const SafeRef&) = delete;
    SafeRef& operator=(const SafeRef&) = delete;

    SafeRef(Reference* s = nullptr) : m_ref(s) {
        if (m_ref != nullptr) {
            m_ref->incr_ref();
        }
    }
    virtual ~SafeRef() {
        if (m_ref != nullptr) {
            m_ref->decr_ref();
        }
    }

   private:
    Reference* m_ref = nullptr;
};

#define PROTECT_REF(x) \
    SafeRef _ref(x);

}  // namespace kim

#endif  //__KIM_REFERENCE_H__