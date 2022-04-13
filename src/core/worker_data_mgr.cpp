#include "worker_data_mgr.h"

#include "server.h"

namespace kim {

WorkerDataMgr::WorkerDataMgr(std::shared_ptr<Log> logger) : Logger(logger) {
    m_itr_worker = m_workers.begin();
}

WorkerDataMgr::~WorkerDataMgr() {
    for (auto& it : m_workers) {
        SAFE_DELETE(it.second);
    }
    m_workers.clear();
    m_itr_worker = m_workers.end();
    m_index_workers.clear();
}

bool WorkerDataMgr::add_worker_info(int index, int pid, const fd_t& fctrl, const fd_t& fdata) {
    auto it = m_workers.find(pid);
    if (it != m_workers.end()) {
        LOG_WARN(
            "duplicate worker info, pid: %d, "
            "old index: %d, old ctrl fd: %d, old data fd: %d, "
            "new index: %d, new ctrl fd: %d, new data fd: %d, ",
            pid, it->second->index, it->second->fctrl.fd, it->second->fdata.fd,
            index, fctrl.fd, fdata.fd);
        del_worker_info(pid);
    }

    auto info = new worker_info_t{pid, index, fctrl, fdata};
    if (info == nullptr) {
        LOG_ERROR("alloc worker_info falied!");
        return false;
    }

    m_workers[pid] = info;
    m_itr_worker = m_workers.begin();
    m_index_workers[index] = info;
    LOG_INFO("add worker info done! pid: %d, index: %d", pid, index);
    return true;
}

int WorkerDataMgr::get_worker_index(int pid) {
    auto it = m_workers.find(pid);
    return (it != m_workers.end()) ? it->second->index : -1;
}

int WorkerDataMgr::get_worker_data_fd(int worker_index) {
    auto it = m_index_workers.find(worker_index);
    return (it != m_index_workers.end()) ? (it->second->fdata.fd) : -1;
}

worker_info_t* WorkerDataMgr::get_worker_info_by_index(int worker_index) {
    auto it = m_index_workers.find(worker_index);
    return (it != m_index_workers.end()) ? (it->second) : nullptr;
}

worker_info_t* WorkerDataMgr::get_worker_info_by_pid(int pid) {
    auto it = m_workers.find(pid);
    if (it != m_workers.end()) {
        return it->second;
    }
    return nullptr;
}

bool WorkerDataMgr::del_worker_info(int pid) {
    auto it = m_workers.find(pid);
    if (it == m_workers.end()) {
        return false;
    }

    worker_info_t* info = it->second;
    m_index_workers.erase(info->index);
    SAFE_DELETE(info);
    m_workers.erase(it);
    LOG_INFO("del worker info, pid: %d", pid);

    m_itr_worker = m_workers.begin();
    return true;
}

bool WorkerDataMgr::get_worker_channel(int pid, int* chs) {
    if (chs == nullptr) {
        return false;
    }

    auto it = m_workers.find(pid);
    if (chs == nullptr || it == m_workers.end() || it->second == nullptr) {
        return false;
    }

    worker_info_t* info = it->second;
    chs[0] = info->fctrl.fd;
    chs[1] = info->fdata.fd;
    return true;
}

int WorkerDataMgr::get_next_worker_data_fd() {
    if (m_workers.empty()) {
        LOG_ERROR("workers is empty!");
        return -1;
    }
    m_itr_worker++;
    if (m_itr_worker == m_workers.end()) {
        m_itr_worker = m_workers.begin();
    }
    return m_itr_worker->second->fdata.fd;
}

}  // namespace kim