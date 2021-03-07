#ifndef __KIM_WORKER_DATA_MGR_H__
#define __KIM_WORKER_DATA_MGR_H__

#include "nodes.h"
#include "protobuf/sys/payload.pb.h"
#include "util/log.h"

namespace kim {

typedef struct worker_info_s {
    int pid;               /* worker's process id. */
    int index;             /* worker's index which assiged by manager. */
    fd_t fctrl;            /* socketpair for parent and child. */
    fd_t fdata;            /* socketpair for parent and child. */
    std::string work_path; /* process work path. */
    Payload payload;       /* payload info. */
} worker_info_t;

class WorkerDataMgr : public Logger {
   public:
    WorkerDataMgr(Log* logger);
    virtual ~WorkerDataMgr();

   public:
    bool del_worker_info(int pid);
    worker_info_t* get_worker_info(int index);
    bool add_worker_info(int index, int pid, const fd_t& fctrl, const fd_t& fdata);
    const std::unordered_map<int, worker_info_t*>& get_infos() const { return m_workers; }

    int get_next_worker_data_fd();
    bool get_worker_channel(int pid, int* chs);
    int get_worker_index(int pid);
    int get_worker_data_fd(int worker_index);

   private:
    /* key: pid. */
    std::unordered_map<int, worker_info_t*> m_workers;
    std::unordered_map<int, worker_info_t*>::iterator m_itr_worker;
    /* key: worker_index. */
    std::unordered_map<int, worker_info_t*> m_index_workers;
};

}  // namespace kim

#endif  //__KIM_WORKER_DATA_MGR_H__