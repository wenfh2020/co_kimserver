
#include <signal.h>

#include <memory>

#include "./libco/co_routine.h"
#include "connection.h"
#include "error.h"
#include "net/anet.h"
#include "util/json/CJsonObject.hpp"
#include "util/util.h"

using namespace kim;

#define MAX_SEND_PACKETS_ONCE 100

enum {
    KP_REQ_TEST_HELLO = 1001,
    KP_RSP_TEST_HELLO = 1002,
    KP_REQ_TEST_MYSQL = 1003,
    KP_RSP_TEST_MYSQL = 1004,
    KP_REQ_TEST_REDIS = 1005,
    KP_RSP_TEST_REDIS = 1006,
    KP_REQ_TEST_SESSION = 1007,
    KP_RSP_TEST_SESSION = 1008,
};

int g_test_cmd = KP_REQ_TEST_HELLO;

int g_packets = 0;
int g_send_cnt = 0;
int g_callback_cnt = 0;
int g_ok_callback_cnt = 0;
int g_err_callback_cnt = 0;
int g_connected_cnt = 0;

int g_test_users = 0;
int g_test_user_packets = 0;

int g_server_port = 3355;
std::string g_server_host = "127.0.0.1";

int g_seq = 0;
char g_errstr[256];
std::shared_ptr<Log> m_logger = nullptr;
double g_begin_time = 0.0;
std::unordered_map<int, std::shared_ptr<kim::Connection>> g_conns;

size_t g_saddr_len;
struct sockaddr g_saddr;

typedef struct statistics_user_data_s {
    int packets = 0;
    int send_cnt = 0;
    int callback_cnt = 0;
} statistics_user_data_t;

int new_seq() { return ++g_seq; }
bool load_logger(const char* path);
bool check_args(int args, char** argv);
void show_statics_result(bool force = false);

void load_signals();
void signal_handler_event(int sig);

/* coroutines. */
void* co_timer(void* arg);
void* co_readwrite(void* arg);

/* connection. */
bool del_connect(std::shared_ptr<Connection> c);
bool check_connect(std::shared_ptr<Connection> c);
bool check_connect(std::shared_ptr<Connection> c);
bool is_connect_ok(std::shared_ptr<Connection> c);
std::shared_ptr<Connection> get_connect(const char* host, int port);

/* protocol. */
bool check_rsp(std::shared_ptr<Connection> c, std::shared_ptr<Msg> msg);
Codec::STATUS send_packets(std::shared_ptr<Connection> c);
Codec::STATUS send_proto(std::shared_ptr<Connection> c, int cmd, const std::string& data);

int main(int args, char** argv) {
    if (!check_args(args, argv)) {
        return 1;
    }

    if (!load_logger("./test.log")) {
        return 1;
    }

    LOG_INFO("start pressure, host: %s, port: %d, users: %d, packets: %d",
             g_server_host.c_str(), g_server_port, g_test_users, g_test_user_packets);

    // g_begin_time = time_now();

    stCoRoutine_t* co;
    for (int i = 0; i < g_test_users; i++) {
        co_create(&co, NULL, co_readwrite);
        co_resume(co);
    }

    /* timer */
    co_create(&co, NULL, co_timer);
    co_resume(co);

    co_eventloop(co_get_epoll_ct());
    return 0;
}

void* co_timer(void* arg) {
    co_enable_hook_sys();

    for (;;) {
        co_sleep(5000);
        show_statics_result(true);
    }

    return 0;
}

void* co_readwrite(void* arg) {
    co_enable_hook_sys();

    int fd = -1;
    bool is_connected = false;

    Codec::STATUS ret;
    statistics_user_data_t* stat;

    auto c = get_connect(g_server_host.c_str(), g_server_port);
    if (c == nullptr) {
        LOG_ERROR("async connect failed, host: %s, port: %d",
                  g_server_host.c_str(), g_server_port);
        return 0;
    }

    auto msg = std::make_shared<Msg>(c->ft());

    for (;;) {
        /* there may be delays connecting to the server. */
        if (!check_connect(c)) {
            co_sleep(1000);
            continue;
        }

        if (!is_connected) {
            is_connected = true;
            g_connected_cnt++;
        }

        /* wait all client connected, then test. */
        if (g_connected_cnt != g_test_users) {
            co_sleep(1000);
            continue;
        }

        if (g_begin_time == 0.0) {
            g_begin_time = time_now();
        }

        fd = c->fd();
        stat = (statistics_user_data_t*)c->privdata();
        ret = c->conn_read(msg);

        while (ret == Codec::STATUS::OK) {
            g_callback_cnt++;
            stat->callback_cnt++;
            LOG_DEBUG("fd: %d, callback cnt: %d.", c->fd(), stat->callback_cnt);

            check_rsp(c, msg) ? g_ok_callback_cnt++ : g_err_callback_cnt++;
            show_statics_result();

            if (stat->callback_cnt == stat->packets) {
                LOG_DEBUG("handle all packets! fd: %d", c->fd());
                del_connect(c);
                return 0;
            }

            msg->head()->Clear();
            msg->body()->Clear();
            ret = c->fetch_data(msg);
            LOG_DEBUG("conn read result, fd: %d, ret: %d", fd, (int)ret);
        }

        show_statics_result();

        if (ret == Codec::STATUS::ERR || ret == Codec::STATUS::CLOSED) {
            if (ret == Codec::STATUS::ERR) {
                g_callback_cnt++;
                g_err_callback_cnt++;
                stat->callback_cnt++;
                LOG_ERROR("conn read failed. fd: %d", fd);
            }
            del_connect(c);
            return 0;
        }

        co_sleep(100);

        ret = send_packets(c);
        if (ret == Codec::STATUS::ERR || ret == Codec::STATUS::CLOSED) {
            del_connect(c);
            LOG_ERROR("conn read failed. fd: %d", fd);
            return 0;
        } else if (ret == Codec::STATUS::PAUSE) {
            co_sleep(100);
            continue;
        }

        co_sleep(1000, fd, POLLIN);
        continue;
    }

    return 0;
}

bool check_args(int args, char** argv) {
    if (args < 5 ||
        argv[2] == nullptr || !isdigit(argv[2][0]) || atoi(argv[2]) == 0 ||
        argv[3] == nullptr || !isdigit(argv[3][0]) || atoi(argv[3]) == 0 ||
        argv[4] == nullptr || !isdigit(argv[4][0]) || atoi(argv[4]) == 0 ||
        argv[5] == nullptr || !isdigit(argv[5][0]) || atoi(argv[5]) == 0) {
        std::cerr << "./test_tcp_pressure [host] [port] [protocol(1001/1003/1005)] [users] [user_packets]"
                  << std::endl;
        return false;
    }

    g_server_host = argv[1];
    g_server_port = atoi(argv[2]);
    g_test_cmd = atoi(argv[3]);
    g_test_users = atoi(argv[4]);
    g_test_user_packets = atoi(argv[5]);
    g_send_cnt = g_test_users * g_test_user_packets;
    return true;
}

bool load_logger(const char* path) {
    m_logger = std::make_shared<kim::Log>();
    if (!m_logger->set_log_path(path)) {
        std::cerr << "set log path failed!" << std::endl;
        return false;
    }
    m_logger->set_level(Log::LL_INFO);
    m_logger->set_worker_index(0);
    m_logger->set_process_type(true);
    return true;
}

std::shared_ptr<Connection> get_connect(const char* host, int port) {
    if (host == nullptr || port == 0) {
        LOG_ERROR("invalid host or port!");
        return nullptr;
    }

    statistics_user_data_t* stat;

    auto fd = anet_tcp_connect(g_errstr, host, port, false, &g_saddr, &g_saddr_len);
    if (fd == -1) {
        LOG_ERROR("client connect server failed! errstr: %s",
                  g_errstr);
        return nullptr;
    }

    if (anet_no_block(g_errstr, fd) != ANET_OK) {
        LOG_ERROR("set socket no block failed! fd: %d, errstr: %s", fd, g_errstr);
        close(fd);
        return nullptr;
    }
    auto c = std::make_shared<Connection>(m_logger, nullptr, fd, new_seq());
    if (c == nullptr) {
        close(fd);
        LOG_ERROR("alloc connection failed! fd: %d", fd);
        return nullptr;
    }

    stat = (statistics_user_data_t*)calloc(1, sizeof(statistics_user_data_t));
    stat->packets = g_test_user_packets;

    c->init(Codec::TYPE::PROTOBUF);
    c->set_state(Connection::STATE::CONNECTING);
    c->set_addr_info(&g_saddr, g_saddr_len);
    c->set_privdata(stat);
    g_conns[fd] = c;
    LOG_INFO("new connection done! fd: %d", fd);
    return c;
}

bool is_connect_ok(std::shared_ptr<Connection> c) {
    bool completed = false;
    if (anet_check_connect_done(
            c->fd(), c->sockaddr(), c->saddr_len(), completed) == ANET_ERR) {
        LOG_ERROR("connect failed! fd: %d", c->fd());
        return false;
    } else {
        if (completed) {
            LOG_DEBUG("connect done! fd: %d", c->fd());
            c->set_state(Connection::STATE::CONNECTED);
        } else {
            LOG_DEBUG("connect not completed! fd: %d", c->fd());
        }
        return true;
    }
}

bool check_connect(std::shared_ptr<Connection> c) {
    if (c->is_connected()) {
        return true;
    }

    if (!is_connect_ok(c)) {
        LOG_DEBUG("connect failed! fd: %d", c->fd());
        g_conns.erase(c->fd());
        return false;
    }

    if (!c->is_connected()) {
        LOG_DEBUG("connect next time! fd: %d", c->fd());
        return false;
    }

    LOG_INFO("connect ok! fd: %d", c->fd());
    return true;
}

bool del_connect(std::shared_ptr<Connection> c) {
    if (c == nullptr) {
        LOG_ERROR("invalid params!");
        return false;
    }

    auto it = g_conns.find(c->fd());
    if (it == g_conns.end()) {
        return false;
    }

    free((statistics_user_data_t*)c->privdata());
    close(c->fd());
    g_conns.erase(it);
    return true;
}

Codec::STATUS send_proto(std::shared_ptr<Connection> c, int cmd, const std::string& data) {
    auto msg = std::make_shared<Msg>(c->ft());

    msg->body()->set_data(data);
    auto body_len = msg->body()->ByteSizeLong();

    msg->head()->set_cmd(cmd);
    msg->head()->set_seq(new_seq());
    msg->head()->set_len(body_len);

    LOG_DEBUG("send fd: %d, seq: %d, body len: %d, data: <%s>",
              c->fd(), msg->head()->seq(), body_len, msg->body()->data().c_str());
    return c->conn_write(msg);
}

Codec::STATUS send_packets(std::shared_ptr<Connection> c) {
    if (c == nullptr || !c->is_connected()) {
        LOG_ERROR("invalid connection!");
        return Codec::STATUS::ERR;
    }

    std::string data;
    Codec::STATUS ret = Codec::STATUS::PAUSE;
    statistics_user_data_t* stat = (statistics_user_data_t*)c->privdata();

    if (stat->send_cnt >= stat->packets) {
        return Codec::STATUS::OK;
    }

    if ((stat->packets > 0 && stat->send_cnt < stat->packets &&
         stat->send_cnt == stat->callback_cnt)) {
        for (int i = 0; i < MAX_SEND_PACKETS_ONCE && i < stat->packets; i++) {
            if (stat->send_cnt >= stat->packets) {
                LOG_INFO("send cnt == packets, fd: %d", c->fd());
                return Codec::STATUS::OK;
            }
            stat->send_cnt++;
            LOG_DEBUG("packets info: fd: %d, packets: %d, send cnt: %d, callback cnt: %d\n",
                      c->fd(), stat->packets, stat->send_cnt, stat->callback_cnt);

            if (g_test_cmd == KP_REQ_TEST_SESSION) {
                CJsonObject packet;
                packet.Add("user_id", (int)c->id());
                packet.Add("user_name", format_str("hello - %d", c->id()));
                data = packet.ToString();
                // printf("data: %s\n", data.c_str());
            } else {
                data = format_str("%d - hello", i + 1);
            }

            ret = send_proto(c, g_test_cmd, data);
            if (ret != Codec::STATUS::OK) {
                return ret;
            }
            continue;
        }
    }

    return ret;
}

bool check_rsp(std::shared_ptr<Connection> c, std::shared_ptr<Msg> msg) {
    if (!msg->body()->has_rsp_result()) {
        LOG_ERROR("no rsp result! fd: %d, cmd: %d", c->fd(), msg->head()->cmd());
        return false;
    }
    if (msg->body()->rsp_result().code() != ERR_OK) {
        LOG_ERROR("rsp code is not ok, error! fd: %d, error: %d, errstr: %s",
                  c->fd(), msg->body()->rsp_result().code(), msg->body()->rsp_result().msg().c_str());
        return false;
    }
    return true;
}

void show_statics_result(bool force) {
    LOG_DEBUG("send cnt: %d, cur callback cnt: %d", g_send_cnt, g_callback_cnt);

    if (force && g_send_cnt == g_callback_cnt) {
        return;
    }

    if (g_send_cnt == g_callback_cnt || force) {
        std::cout << "------" << std::endl
                  << "spend time: " << time_now() - g_begin_time << std::endl
                  << "avg:        " << g_callback_cnt / (time_now() - g_begin_time) << std::endl;

        std::cout << "send cnt:         " << g_send_cnt << std::endl
                  << "callback cnt:     " << g_callback_cnt << std::endl
                  << "ok callback cnt:  " << g_ok_callback_cnt << std::endl
                  << "err callback cnt: " << g_err_callback_cnt << std::endl;
    }
}