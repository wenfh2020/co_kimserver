#include "worker.h"

#include <signal.h>

#include <random>

#include "util/set_proc_title.h"
#include "util/util.h"

namespace kim {

void* Worker::m_signal_user_data = nullptr;

Worker::Worker(const std::string& name) {
    srand((unsigned)time(NULL));
    set_proc_title("%s", name.c_str());
}

Worker::~Worker() {
    SAFE_DELETE(m_net);
    SAFE_DELETE(m_logger);
}

bool Worker::init(const worker_info_t* info, const CJsonObject& config) {
    m_worker_info.work_path = info->work_path;
    m_worker_info.fctrl = info->fctrl;
    m_worker_info.fdata = info->fdata;
    m_worker_info.index = info->index;
    m_worker_info.pid = getpid();

    m_config = config;

    if (!load_logger()) {
        LOG_ERROR("init log failed!");
        return false;
    }

    LOG_INFO("init worker, index: %d, fctrl fd: %d, fdata: %d",
             info->index, info->fctrl.fd, info->fdata.fd);

    if (!load_network()) {
        LOG_ERROR("create network failed!");
        return false;
    }

    init_timer();
    return true;
}

bool Worker::load_logger() {
    char path[MAX_PATH] = {0};
    snprintf(path, sizeof(path), "%s/%s", m_worker_info.work_path.c_str(),
             m_config("log_path").c_str());

    m_logger = new Log;
    if (m_logger == nullptr) {
        return false;
    }

    if (!m_logger->set_log_path(path)) {
        LOG_ERROR("set log path failed! path: %s", path);
        return false;
    }

    if (!m_logger->set_level(m_config("log_level").c_str())) {
        LOG_ERROR("invalid log level!");
        return false;
    }

    m_logger->set_process_type(false);
    m_logger->set_worker_index(m_worker_info.index);

    load_signals();
    LOG_INFO("init logger done!");
    return true;
}

bool Worker::load_network() {
    LOG_TRACE("load network!");

    m_net = new Network(m_logger, Network::TYPE::WORKER);
    if (m_net == nullptr) {
        LOG_ERROR("new network failed!");
        return false;
    }

    if (!m_net->create_w(m_config, m_worker_info.fctrl.fd, m_worker_info.fdata.fd,
                         m_worker_info.index)) {
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

void Worker::on_repeat_timer() {
    co_enable_hook_sys();
    if (m_net != nullptr) {
        m_net->on_timer();
    }
}

std::string Worker::worker_name(int index) {
    return format_str("%s_w_%d", m_config("server_name").c_str(), index);
}

void Worker::load_signals() {
    struct sigaction act;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = &signal_handler;

    int signals[] = {SIGINT, SIGKILL, SIGTERM, SIGUSR1};
    for (unsigned int i = 0; i < sizeof(signals) / sizeof(int); i++) {
        sigaction(signals[i], &act, 0);
    }

    signal(SIGPIPE, SIG_IGN);
    m_signal_user_data = this;
}

void Worker::signal_handler(int sig) {
    Worker* m = (Worker*)m_signal_user_data;
    m->signal_handler_event(sig);
}

void Worker::signal_handler_event(int sig) {
    std::string name = worker_name(m_worker_info.index);
    LOG_CRIT("%s terminated by signal %d!", name.c_str(), sig);
    _exit(EXIT_CHILD);
}

}  // namespace kim