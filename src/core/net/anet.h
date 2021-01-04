/* https://github.com/redis/redis/blob/unstable/src/anet.c */

#ifndef __ANET_H__
#define __ANET_H__

#include <sys/socket.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

namespace kim {

#define ANET_OK 0
#define ANET_ERR -1
#define ANET_ERR_LEN 256

int anet_tcp_server(char *err, const char *bindaddr, int port, int backlog);

int anet_block(char *err, int fd);
int anet_no_block(char *err, int fd);

int anet_tcp_accept(char *err, int s, char *ip, size_t ip_len, int *port, int *family);
int anet_keep_alive(char *err, int fd, int interval);
int anet_set_tcp_no_delay(char *err, int fd, int val);

int anet_tcp_connect(
    char *err, const char *host, int port,
    bool is_block = false, struct sockaddr *saddr = nullptr, size_t *saddrlen = nullptr);

int anet_check_connect_done(int fd, struct sockaddr *saddr, size_t saddr_len, bool &completed);

}  // namespace kim

#ifdef __cplusplus
}
#endif

#endif
