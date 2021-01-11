#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <iostream>

#include "./libco/co_routine.h"
#include "connection.h"
#include "net/anet.h"
#include "server.h"
#include "util/log.h"
#include "util/util.h"

using namespace kim;

/* 需求：
 * 1. 开 1000 个用户，每个用户发 1000 个包。
 * 2. 统计数据。
 * */

int g_packets = 0;
int g_send_cnt = 0;
int g_callback_cnt = 0;

int g_test_users = 0;
int g_test_packets = 0;

int g_server_port = 3355;
std::string g_server_host = "127.0.0.1";

Log* m_logger = nullptr;
char g_errstr[256];
int g_seq = 0;

size_t g_saddr_len;
struct sockaddr g_saddr;

std::unordered_map<int, kim::Connection*> g_conns;

typedef struct statistics_data_s {
    int packets = 0;
    int send_cnt = 0;
    int callback_cnt = 0;
} statistics_data_t;

enum {
    KP_REQ_TEST_PROTO = 1001,
    KP_RSP_TEST_PROTO = 1002,
    KP_REQ_TEST_AUTO_SEND = 1003,
    KP_RSP_TEST_AUTO_SEND = 1004,
};

bool check_args(int args, char** argv) {
    if (args < 4 ||
        argv[2] == nullptr || !isdigit(argv[2][0]) || atoi(argv[2]) == 0 ||
        argv[3] == nullptr || !isdigit(argv[3][0]) || atoi(argv[3]) == 0 ||
        argv[4] == nullptr || !isdigit(argv[4][0]) || atoi(argv[4]) == 0) {
        std::cerr << "./test_tcp_pressure [host] [port] [users] [user_packets]" << std::endl;
        return false;
    }

    g_server_host = argv[1];
    g_server_port = atoi(argv[2]);
    g_test_users = atoi(argv[3]);
    g_test_packets = atoi(argv[4]);
    return true;
}

int new_seq() { return ++g_seq; }

bool load_logger(const char* path) {
    m_logger = new kim::Log;
    if (!m_logger->set_log_path(path)) {
        std::cerr << "set log path failed!" << std::endl;
        return false;
    }
    m_logger->set_level(Log::LL_DEBUG);
    m_logger->set_worker_index(0);
    m_logger->set_process_type(true);
    return true;
}

Connection* get_connect(const char* host, int port) {
    if (host == nullptr || port == 0) {
        LOG_ERROR("invalid params!");
        return nullptr;
    }

    int fd;
    Connection* c;

    fd = anet_tcp_connect(g_errstr, host, port, false, &g_saddr, &g_saddr_len);
    if (fd == -1) {
        LOG_ERROR("client connect server failed! errstr: %s",
                  g_errstr);
        return nullptr;
    }

    // connection.
    c = new Connection(m_logger, fd, 0);
    if (c == nullptr) {
        close(fd);
        LOG_ERROR("alloc connection failed! fd: %d", fd);
        return nullptr;
    }

    statistics_data_t* stat_data = (statistics_data_t*)calloc(1, sizeof(statistics_data_t));
    stat_data->packets = g_test_packets;

    c->init(Codec::TYPE::PROTOBUF);
    c->set_state(Connection::STATE::CONNECTING);
    c->set_addr_info(&g_saddr, g_saddr_len);
    c->set_privdata(stat_data);
    g_conns[fd] = c;
    LOG_INFO("new connection done! fd: %d", fd);
    return c;
}

bool is_connect_ok(Connection* c) {
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

bool check_connect(Connection* c) {
    if (!c->is_connected()) {
        if (!is_connect_ok(c)) {
            LOG_DEBUG("connect failed! fd: %d", c->fd());
            g_conns.erase(c->fd());
            return false;
        }
        if (!c->is_connected()) {
            LOG_DEBUG("connect next time! fd: %d", c->fd());
            return false;
        }
        LOG_DEBUG("connect ok! fd: %d", c->fd());
    }
    return true;
}

Codec::STATUS send_proto(Connection* c, int cmd, const std::string& data) {
    MsgHead head;
    MsgBody body;
    size_t body_len;

    body.set_data(data);
    body_len = body.ByteSizeLong();

    head.set_cmd(cmd);
    head.set_seq(new_seq());
    head.set_len(body_len);

    LOG_DEBUG("send fd: %d, seq: %d, body len: %d, data: <%s>",
              c->fd(), head.seq(), body_len, body.data().c_str());
    return c->conn_write(head, body);
}

Codec::STATUS send_packets(Connection* c) {
    if (c == nullptr || !c->is_connected()) {
        LOG_ERROR("invalid connection!");
        return Codec::STATUS::ERR;
    }

    Codec::STATUS ret = Codec::STATUS::PAUSE;
    statistics_data_t* stat = (statistics_data_t*)c->privdata();

    if ((stat->packets > 0 && stat->send_cnt < stat->packets &&
         stat->send_cnt == stat->callback_cnt)) {
        for (int i = 0; i < 100 && i < stat->packets; i++) {
            if (stat->send_cnt >= stat->packets) {
                LOG_INFO("send cnt == packets, fd: %d", c->fd());
                return Codec::STATUS::CLOSED;
            }
            stat->send_cnt++;
            LOG_DEBUG("packets info: fd: %d, packets: %d, send cnt: %d, callback cnt: %d\n",
                      c->fd(), stat->packets, stat->send_cnt, stat->callback_cnt);
            ret = send_proto(c, KP_REQ_TEST_PROTO, format_str("%d - hello", i));
            if (ret != Codec::STATUS::OK) {
                return ret;
            }
            continue;
        }
    }

    return ret;
}

bool del_connect(Connection* c) {
    if (c == nullptr) {
        LOG_ERROR("invalid params!");
        return false;
    }
    auto it = g_conns.find(c->fd());
    if (it == g_conns.end()) {
        return false;
    }
    free((statistics_data_t*)c->privdata());
    close(c->fd());
    SAFE_DELETE(it->second);
    g_conns.erase(it);
    return true;
}

void co_sleep(int ms, int fd = -1, int events = 0) {
    struct pollfd pf = {0};
    pf.fd = fd;
    pf.events = events | POLLERR | POLLHUP;
    poll(&pf, 1, ms);
}

static void* readwrite_routine(void* arg) {
    co_enable_hook_sys();

    int fd = -1;
    Connection* c;
    MsgHead head;
    MsgBody body;
    Codec::STATUS codec_ret;
    statistics_data_t* stat;

    c = get_connect(g_server_host.c_str(), g_server_port);
    if (c == nullptr) {
        LOG_ERROR("async connect failed, host: %s, port: %d",
                  g_server_host.c_str(), g_server_port);
        return 0;
    }

    for (;;) {
        if (!check_connect(c)) {
            co_sleep(1000, fd, POLLIN);
            continue;
        }

        fd = c->fd();
        stat = (statistics_data_t*)c->privdata();
        codec_ret = c->conn_read(head, body);

        while (codec_ret == Codec::STATUS::OK) {
            stat->callback_cnt++;
            LOG_DEBUG("callback cnt: %d, fd: %d", stat->callback_cnt, c->fd());
            if (stat->callback_cnt == stat->packets) {
                LOG_INFO("handle all packets! fd: %d", c->fd());
                del_connect(c);
                return 0;
            }
            head.Clear();
            body.Clear();
            codec_ret = c->fetch_data(head, body);
            LOG_DEBUG("conn read result, fd: %d, ret: %d", fd, (int)codec_ret);
        }

        if (codec_ret == Codec::STATUS::ERR ||
            codec_ret == Codec::STATUS::CLOSED) {
            LOG_ERROR("conn read failed. fd: %d", fd);
            return 0;
        }

        codec_ret = send_packets(c);
        if (codec_ret == Codec::STATUS::ERR ||
            codec_ret == Codec::STATUS::CLOSED) {
            printf("55555\n");
            del_connect(c);
            LOG_ERROR("conn read failed. fd: %d", fd);
            return 0;
        } else if (codec_ret == Codec::STATUS::PAUSE) {
            co_sleep(1000, fd);
            continue;
        }

        co_sleep(1000, fd, POLLIN);
        continue;
    }
    return 0;
}

int main(int args, char** argv) {
    if (!check_args(args, argv)) {
        return 1;
    }

    if (!load_logger("./test.log")) {
        return 1;
    }

    LOG_INFO("start pressure, host: %s, port: %d, users: %d, packets: %d",
             g_server_host.c_str(), g_server_port, g_test_users, g_test_packets);

    for (int i = 0; i < g_test_users; i++) {
        stCoRoutine_t* co;
        co_create(&co, NULL, readwrite_routine, nullptr);
        co_resume(co);
    }
    co_eventloop(co_get_epoll_ct(), 0, 0);

    exit(0);
    return 0;
}
