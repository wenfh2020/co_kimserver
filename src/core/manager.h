#ifndef __KIM_MANAGER_H__
#define __KIM_MANAGER_H__

#include "network.h"

namespace kim {

class Manager {
   public:
    Manager();
    virtual ~Manager();

    bool init(const char* conf_path);
    void destory();
    void run();

   private:
    bool load_logger();
    bool load_network();
    bool load_config(const char* path);

    void create_workers();                /* fork children. */
    bool create_worker(int worker_index); /* creates the specified index process. */
    bool restart_worker(pid_t pid);       /* restart the specified pid process. */
    void restart_workers();               /* delay restart of a process that has been shut down. */

    std::string worker_name(int index);

    static void* co_handle_timer(void* arg);

   private:
    Log* m_logger = nullptr;  /* logger. */
    Network* m_net = nullptr; /* net work. */

    node_info m_node_info;          /* cluster node. */
    CJsonObject m_conf, m_old_conf; /* config. */
};

}  // namespace kim

#endif  //__KIM_MANAGER_H__
