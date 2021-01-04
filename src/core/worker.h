#ifndef __KIM_WORKER_H__
#define __KIM_WORKER_H__

#include "network.h"
#include "nodes.h"
#include "util/json/CJsonObject.hpp"
#include "worker_data_mgr.h"

namespace kim {

class Worker {
   public:
    Worker(const std::string& name);
    virtual ~Worker();

    bool init(const worker_info_t* info, const CJsonObject& conf);
    void run();

   private:
    bool load_logger();
    bool load_network();

   private:
    Log* m_logger = nullptr;      /* logger. */
    Network* m_net = nullptr;     /* network. */
    CJsonObject m_conf;           // current config.
    worker_info_t m_worker_info;  // current worker info.
};

}  // namespace kim

#endif  //__KIM_WORKER_H__
