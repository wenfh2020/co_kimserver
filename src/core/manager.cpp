#include "manager.h"

#include <signal.h>

#include "net/anet.h"
#include "server.h"
#include "util/set_proc_title.h"
#include "util/util.h"
#include "worker.h"

namespace kim {

void* Manager::m_signal_user_data = nullptr;

Manager::Manager() {
}

Manager::~Manager() {
    destroy();
}

void Manager::destroy() {
    m_net = nullptr;
    m_config = nullptr;
    m_logger = nullptr;
}

void Manager::run() {
    if (m_net != nullptr) {
        m_net->run();
    }
}

bool Manager::init(const char* config_path) {
    if (!load_sys_config(config_path)) {
        std::cerr << "load sys config failed! config path: "
                  << config_path
                  << std::endl;
        return false;
    }

    if (!load_logger()) {
        std::cerr << "load logger failed!" << std::endl;
        return false;
    }

    set_proc_title("%s", (*m_config->config())("server_name").c_str());

    if (!load_network()) {
        LOG_ERROR("create network failed!");
        return false;
    }

    create_workers();
    load_signals();
    init_timer();
    LOG_INFO("init manager done!");
    return true;
}

void Manager::on_repeat_timer() {
    co_enable_hook_sys();

    run_with_period(1000) {
        restart_workers();
    }

    if (m_net != nullptr) {
        m_net->on_timer();
    }
}

bool Manager::load_logger() {
    if (m_config == nullptr) {
        std::cerr << "pls init config firstly!" << std::endl;
        return false;
    }

    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s",
             m_config->work_path().c_str(), m_config->log_path().c_str());

    m_logger = std::make_shared<Log>();
    if (!m_logger->set_log_path(path)) {
        LOG_ERROR("set log path failed! path: %s", path);
        return false;
    }

    if (!m_logger->set_level(m_config->log_level().c_str())) {
        LOG_ERROR("invalid log level!");
        return false;
    }

    m_logger->set_worker_index(0);
    m_logger->set_process_type(true);

    LOG_INFO("init logger done!");
    return true;
}

bool Manager::load_network() {
    m_net = std::make_shared<Network>(m_logger, Network::TYPE::MANAGER);
    if (m_net == nullptr) {
        LOG_ERROR("new network failed!");
        return false;
    }

    if (!m_net->create_m(m_config)) {
        LOG_ERROR("init network failed!");
        return false;
    }

    LOG_INFO("init network done!");
    return true;
}

bool Manager::load_sys_config(const std::string& config_path) {
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

bool Manager::create_worker(int worker_index) {
    int pid, data_fds[2], ctrl_fds[2];
    auto config_path = m_config->config_path();
    auto work_path = m_config->work_path();
    auto worker_name = m_config->worker_name(worker_index);

    if (socketpair(PF_UNIX, SOCK_STREAM, 0, ctrl_fds) < 0) {
        LOG_ERROR("create socket pair failed! %d: %s", errno, strerror(errno));
        return false;
    }

    if (socketpair(PF_UNIX, SOCK_STREAM, 0, data_fds) < 0) {
        LOG_ERROR("create socket pair failed! %d: %s", errno, strerror(errno));
        m_net->close_channel(ctrl_fds);
        return false;
    }

    if ((pid = fork()) == 0) {
        /* child. */
        destroy();

        close(ctrl_fds[0]);
        close(data_fds[0]);

        fd_t fctrl{ctrl_fds[1], 0};
        fd_t fdata{data_fds[1], 0};

        worker_info_t info{0, worker_index, fctrl, fdata, work_path};
        Worker worker(worker_name);
        if (!worker.init(&info, config_path)) {
            _exit(EXIT_CHILD_INIT_FAIL);
        }
        worker.run();
        _exit(EXIT_CHILD);
    } else if (pid > 0) {
        /* parent. */
        close(ctrl_fds[1]);
        close(data_fds[1]);

        fd_t fctrl{ctrl_fds[0], 0};
        fd_t fdata{data_fds[0], 0};

        if (!m_net->init_manager_channel(fctrl, fdata)) {
            LOG_CRIT("channel fd add event failed! kill child: %d", pid);
            kill(pid, SIGKILL);
            return false;
        }

        m_net->worker_data_mgr()->add_worker_info(worker_index, pid, fctrl, fdata);
        LOG_INFO("manager ctrl fd: %d, data fd: %d", fctrl.fd, fdata.fd);
        return true;
    } else {
        m_net->close_channel(data_fds);
        m_net->close_channel(ctrl_fds);
        LOG_ERROR("error: %d, %s", errno, strerror(errno));
    }

    return false;
}

void Manager::create_workers() {
    for (int i = 1; i <= m_config->worker_cnt(); i++) {
        if (!create_worker(i)) {
            LOG_ERROR("create worker failed! index: %d", i);
        }
    }
}

void Manager::restart_workers() {
    int worker_index;

    while (!m_restart_workers.empty()) {
        worker_index = m_restart_workers.front();
        m_restart_workers.pop();

        LOG_DEBUG("restart worker, index: %d", worker_index);

        if (create_worker(worker_index)) {
            LOG_INFO("restart worker ok! index: %d", worker_index);
        } else {
            LOG_ERROR("create worker failed! index: %d", worker_index);
        }
    }
}

bool Manager::restart_worker(pid_t pid) {
    // int chs[2];
    int worker_index;

    // /* close (manager/worker) channels. */
    // if (m_net->worker_data_mgr()->get_worker_channel(pid, chs)) {
    //     m_net->close_conn(chs[0]);
    //     m_net->close_conn(chs[1]);
    // }

    /* restart worker by worker index. */
    worker_index = m_net->worker_data_mgr()->get_worker_index(pid);
    if (worker_index == -1) {
        LOG_ERROR("can not find work info. pid: %d", pid);
        return false;
    }

    /* work in timer. */
    m_net->worker_data_mgr()->del_worker_info(pid);
    m_restart_workers.push(worker_index);
    LOG_INFO("restart worker, index: %d", worker_index);
    return true;
}

void Manager::close_workers() {
    if (m_net == nullptr) {
        return;
    }
    auto worker_mgr = m_net->worker_data_mgr();
    if (worker_mgr != nullptr) {
        const auto& infos = worker_mgr->get_infos();
        for (const auto& it : infos) {
            kill(it.second->pid, SIGUSR1);
        }
    }
}

void Manager::load_signals() {
    struct sigaction act;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = &signal_handler;

    int signals[] = {SIGCHLD, SIGINT, SIGTERM, SIGSEGV, SIGILL, SIGBUS, SIGFPE, SIGKILL};
    for (unsigned int i = 0; i < sizeof(signals) / sizeof(int); i++) {
        sigaction(signals[i], &act, 0);
    }

    m_signal_user_data = this;
}

void Manager::signal_handler(int sig) {
    Manager* m = (Manager*)m_signal_user_data;
    m->signal_handler_event(sig);
}

void Manager::signal_handler_event(int sig) {
    if (sig == SIGCHLD) {
        int pid, status, ret;
        /* use 'while' for recovering as many signals as possible each time. */
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            if (WIFEXITED(status)) {
                ret = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                ret = WTERMSIG(status);
            } else if (WIFSTOPPED(status)) {
                ret = WSTOPSIG(status);
            }
            LOG_CRIT("child terminated! pid: %d, signal %d, error: %d, ret: %d!",
                     pid, sig, status, ret);
            // restart_worker(pid);
        }
    } else {
        LOG_CRIT("%s terminated by signal %d!",
                 m_config->server_name().c_str(), sig);

        close_workers();

        if (m_net != nullptr && m_net->zk_client() != nullptr) {
            m_net->zk_client()->close_my_node();
        }

        sleep(1);
        exit(sig);
    }
}

}  // namespace kim