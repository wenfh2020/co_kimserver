#ifndef __KIM_MANAGER_H__
#define __KIM_MANAGER_H__

#include "network.h"
#include "timer.h"

namespace kim {

class Manager : CoTimer {
   public:
    Manager();
    virtual ~Manager();

    bool init(const char* conf_path);
    void destory();
    void run();

   private:
    bool load_logger();
    bool load_network();
    bool load_sys_config(const std::string& conf_path);

    void create_workers();                /* fork children. */
    bool create_worker(int worker_index); /* creates the specified index process. */
    bool restart_worker(pid_t pid);       /* restart the specified pid process. */
    void restart_workers();               /* delay restart of a process that has been shut down. */
    void close_workers();                 /* notify workers to close. */
    virtual void on_repeat_timer() override;

    /* signals. */
    void load_signals();
    void signal_handler_event(int sig);
    static void signal_handler(int sig);

   private:
    Log* m_logger = nullptr;     /* logger. */
    SysConfig* m_conf = nullptr; /* system config data. */
    Network* m_net = nullptr;    /* net work. */
    static void* m_signal_user_data;
    std::queue<int> m_restart_workers; /* workers waiting to restart. restore worker's index. */
};

}  // namespace kim

#endif  //__KIM_MANAGER_H__
