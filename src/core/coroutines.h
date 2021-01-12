#ifndef __KIM_COROUTINES_H__
#define __KIM_COROUTINES_H__

#include "connection.h"
#include "libco/co_routine.h"
#include "server.h"
#include "util/log.h"

#define MAX_CO_CNT 100000

namespace kim {

typedef struct co_task_s {
    Connection* c;
    stCoRoutine_t* co;
} co_task_t;

class Coroutines {
   public:
    Coroutines(Log* logger);
    virtual ~Coroutines();

    void clear_tasks();

    int get_max_co_cnt() { return m_max_co_cnt; }
    void set_max_co_cnt(int cnt) { m_max_co_cnt = cnt; }
    co_task_t* create_co_task(Connection* c, pfn_co_routine_t fn);
    bool add_free_co_task(co_task_t* task);

    void co_sleep(int ms, int fd = -1, int events = 0);

   private:
    Log* m_logger = nullptr;
    std::set<co_task_t*> m_coroutines;
    std::set<co_task_t*> m_co_free;
    int m_max_co_cnt = MAX_CO_CNT;
    stCoRoutineAttr_t m_co_attr;
};

}  // namespace kim

#endif  //__KIM_COROUTINES_H__