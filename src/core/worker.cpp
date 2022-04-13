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
}

bool Worker::load_sys_config(const std::string& config_path) {
    m_config = std::make_shared<SysConfig>();
    if (m_config == nullptr) {
        LOG_ERROR("alloc sys config failed!");
        return false;
    }

    if (!m_config->init(config_path)) {
        LOG_ERROR("init sys config failed!");
        return false;
    }
    return true;
}

bool Worker::init(const worker_info_t* info, const std::string& config_path) {
    if (info == nullptr || config_path.empty()) {
        std::cerr << "invalid param!" << std::endl;
        return false;
    }

    if (!load_sys_config(config_path)) {
        std::cerr << "load sys config failed! config path: "
                  << config_path
                  << std::endl;
        return false;
    }

    m_worker_info.work_path = info->work_path;
    m_worker_info.fctrl = info->fctrl;
    m_worker_info.fdata = info->fdata;
    m_worker_info.index = info->index;
    m_worker_info.pid = getpid();

    LOG_INFO("init worker, index: %d, fctrl fd: %d, fdata: %d",
             info->index, info->fctrl.fd, info->fdata.fd);

    if (!load_logger()) {
        std::cerr << "init log failed!" << std::endl;
        return false;
    }

    load_signals();

    if (!load_network()) {
        LOG_ERROR("create network failed!");
        return false;
    }

    init_timer();
    return true;
}

bool Worker::load_logger() {
    if (m_config == nullptr) {
        std::cerr << "pls init config firstly!" << std::endl;
        return false;
    }

    char path[MAX_PATH] = {0};
    snprintf(path, sizeof(path), "%s/%s", m_config->work_path().c_str(),
             m_config->log_path().c_str());

    m_logger = std::make_shared<Log>();
    if (!m_logger->set_log_path(path)) {
        LOG_ERROR("set log path failed! path: %s", path);
        return false;
    }

    if (!m_logger->set_level(m_config->log_level().c_str())) {
        LOG_ERROR("invalid log level!");
        return false;
    }

    m_logger->set_process_type(false);
    m_logger->set_worker_index(m_worker_info.index);

    LOG_INFO("init logger done!");
    return true;
}

bool Worker::load_network() {
    LOG_TRACE("load network!");

    m_net = std::make_shared<Network>(m_logger, Network::TYPE::WORKER);
    if (m_net == nullptr) {
        LOG_ERROR("new network failed!");
        return false;
    }

    if (!m_net->create_w(m_config, m_worker_info.fctrl.fd, m_worker_info.fdata.fd,
                         m_worker_info.index)) {
        LOG_ERROR("init network failed!");
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
    std::string name = m_config->worker_name(m_worker_info.index);
    if (sig == SIGUSR1 || sig == SIGINT) {
        LOG_INFO("%s terminated by signal %d!", name.c_str(), sig);
    } else {
        LOG_CRIT("%s terminated by signal %d!", name.c_str(), sig);
    }
    _exit(EXIT_CHILD);
}

}  // namespace kim