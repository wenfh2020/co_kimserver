#include "worker.h"

#include <random>

#include "util/set_proc_title.h"
#include "util/util.h"

namespace kim {

Worker::Worker(const std::string& name) {
    srand((unsigned)time(NULL));
    set_proc_title("%s", name.c_str());
}

Worker::~Worker() {
    SAFE_DELETE(m_net);
    SAFE_DELETE(m_logger);
}

bool Worker::init(const worker_info_t* info, const CJsonObject& conf) {
    m_worker_info.work_path = info->work_path;
    m_worker_info.ctrl_fd = info->ctrl_fd;
    m_worker_info.data_fd = info->data_fd;
    m_worker_info.index = info->index;
    m_worker_info.pid = getpid();

    m_conf = conf;

    if (!load_logger()) {
        LOG_ERROR("init log failed!");
        return false;
    }

    LOG_INFO("init worker, index: %d, ctrl_fd: %d, data_fd: %d",
             info->index, info->ctrl_fd, info->data_fd);

    if (!load_network()) {
        LOG_ERROR("create network failed!");
        return false;
    }

    return true;
}

bool Worker::load_logger() {
    char path[MAX_PATH] = {0};
    snprintf(path, sizeof(path), "%s/%s", m_worker_info.work_path.c_str(),
             m_conf("log_path").c_str());

    m_logger = new Log;
    if (m_logger == nullptr) {
        return false;
    }

    if (!m_logger->set_log_path(path)) {
        LOG_ERROR("set log path failed! path: %s", path);
        return false;
    }

    if (!m_logger->set_level(m_conf("log_level").c_str())) {
        LOG_ERROR("invalid log level!");
        return false;
    }

    m_logger->set_process_type(false);
    m_logger->set_worker_index(m_worker_info.index);
    return true;
}

bool Worker::load_network() {
    LOG_TRACE("load network!");

    m_net = new Network(m_logger, Network::TYPE::WORKER);
    if (m_net == nullptr) {
        LOG_ERROR("new network failed!");
        return false;
    }

    if (!m_net->create_w(m_conf, m_worker_info.ctrl_fd,
                         m_worker_info.data_fd, m_worker_info.index)) {
        LOG_ERROR("init network failed!");
        SAFE_DELETE(m_net);
        return false;
    }

    LOG_INFO("load net work done!");
    return true;
}

void Worker::run() {
    if (m_net != nullptr) {
        m_net->run();
    }
}

}  // namespace kim