#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>

#include "protobuf/proto/msg.pb.h"

#define SEND_PACKETS 1
#define PROTO_MSG_HEAD_LEN 15

enum {
    KP_REQ_TEST_HELLO = 1001,
    KP_RSP_TEST_HELLO = 1002,
};

int g_test_request = KP_REQ_TEST_HELLO;

int test_server(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "./test_tcp [ip] [port]" << std::endl;
        return -1;
    }

    int fd, ret;
    MsgHead head;
    MsgBody body;
    char buf[256];

    int packets = SEND_PACKETS;
    const char* ip = argv[1];
    const char* port = argv[2];
    struct addrinfo hints, *servinfo;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    fd = socket(PF_INET, SOCK_STREAM, 0);
    ret = getaddrinfo(ip, port, &hints, &servinfo);
    if (ret != 0) {
        printf("getaddrinfo err: %d, errstr: %s \n", ret, gai_strerror(ret));
        return -1;
    }

    ret = connect(fd, servinfo->ai_addr, servinfo->ai_addrlen);
    if (ret == -1) {
        printf("connect err: %d, errstr: %s \n", ret, gai_strerror(ret));
        close(fd);
    }
    freeaddrinfo(servinfo);

    while (packets-- > 0) {
        /* send. */
        body.set_data("hello world!");
        head.set_seq(123);
        head.set_cmd(g_test_request);
        head.set_len(body.ByteSizeLong());

        memcpy(buf, head.SerializeAsString().c_str(), head.ByteSizeLong());
        memcpy(buf + head.ByteSizeLong(), body.SerializeAsString().c_str(), body.ByteSizeLong());

        ret = send(fd, buf, head.ByteSizeLong() + body.ByteSizeLong(), 0);
        if (ret < 0) {
            printf("send err: %d, errstr: %s \n", ret, gai_strerror(ret));
            break;
        }

        printf("send req: %d, cmd: %d, seq: %d, len: %d, body len: %zu, %s\n",
               packets, head.cmd(), head.seq(), head.len(),
               body.data().length(), body.data().c_str());

        /* recv. */
        ret = recv(fd, buf, sizeof(buf), 0);
        if (ret <= 0) {
            printf("recv err: %d, errstr: %s \n", ret, gai_strerror(ret));
            break;
        }

        head.ParseFromArray(buf, PROTO_MSG_HEAD_LEN);
        body.ParseFromArray(buf + PROTO_MSG_HEAD_LEN, head.len());
        printf("recv ack: %d, cmd: %d, seq: %d, len: %d, body len: %zu, %s, err: %d, errstr: %s\n",
               packets, head.cmd(), head.seq(), head.len(),
               body.data().length(), body.data().c_str(),
               body.mutable_rsp_result()->code(),
               body.mutable_rsp_result()->msg().c_str());
    }

    close(fd);
    return 0;
}

int main(int argc, char** argv) {
    return test_server(argc, argv);
    return 0;
}