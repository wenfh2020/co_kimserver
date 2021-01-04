#include "anet.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

namespace kim {

static void anet_set_error(char *err, const char *fmt, ...) {
    va_list ap;

    if (!err) return;
    va_start(ap, fmt);
    vsnprintf(err, ANET_ERR_LEN, fmt, ap);
    va_end(ap);
}

static int anet_set_block(char *err, int fd, int non_block) {
    int flags;

    if ((flags = fcntl(fd, F_GETFL)) == -1) {
        anet_set_error(err, "fcntl(F_GETFL): %s", strerror(errno));
        return ANET_ERR;
    }

    if (non_block) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }

    if (fcntl(fd, F_SETFL, flags) == -1) {
        anet_set_error(err, "fcntl(F_SETFL,O_NONBLOCK): %s", strerror(errno));
        return ANET_ERR;
    }
    return ANET_OK;
}

int anet_no_block(char *err, int fd) {
    return anet_set_block(err, fd, 1);
}

int anet_block(char *err, int fd) {
    return anet_set_block(err, fd, 0);
}

static int anet_listen(char *err, int s, struct sockaddr *sa, socklen_t len,
                       int backlog) {
    if (bind(s, sa, len) == -1) {
        anet_set_error(err, "bind: %s", strerror(errno));
        close(s);
        return ANET_ERR;
    }

    if (listen(s, backlog) == -1) {
        anet_set_error(err, "listen: %s", strerror(errno));
        close(s);
        return ANET_ERR;
    }
    return ANET_OK;
}

static int anet_set_reuse_addr(char *err, int fd) {
    int yes = 1;
    /* Make sure connection-intensive things like the redis benchmark
     * will be able to close/open sockets a zillion of times */
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        anet_set_error(err, "setsockopt SO_REUSEADDR: %s", strerror(errno));
        return ANET_ERR;
    }
    return ANET_OK;
}

static int anet_v6_only(char *err, int s) {
    int yes = 1;
    if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof(yes)) == -1) {
        anet_set_error(err, "setsockopt: %s", strerror(errno));
        close(s);
        return ANET_ERR;
    }
    return ANET_OK;
}

int anet_tcp_server(char *err, const char *bindaddr, int port, int backlog) {
    int s = -1, rv;
    char _port[6]; /* strlen("65535") */
    struct addrinfo hints, *servinfo, *p;

    snprintf(_port, 6, "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; /* No effect if bindaddr != NULL */

    if ((rv = getaddrinfo(bindaddr, _port, &hints, &servinfo)) != 0) {
        anet_set_error(err, "%s", gai_strerror(rv));
        return ANET_ERR;
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((s = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
            continue;

        if (p->ai_family == AF_INET6 && anet_v6_only(err, s) == ANET_ERR) goto error;
        if (anet_set_reuse_addr(err, s) == ANET_ERR) goto error;
        if (anet_listen(err, s, p->ai_addr, p->ai_addrlen, backlog) == ANET_ERR)
            s = ANET_ERR;
        goto end;
    }

    if (p == NULL) {
        anet_set_error(err, "unable to bind socket, errno: %d", errno);
        goto error;
    }

error:
    if (s != -1) close(s);
    s = ANET_ERR;

end:
    freeaddrinfo(servinfo);
    return s;
}

static int anet_generic_accept(char *err, int s, struct sockaddr *sa,
                               socklen_t *len) {
    int fd;
    while (1) {
        fd = accept(s, sa, len);
        if (fd == -1) {
            if (errno == EINTR) {
                continue;
            } else {
                anet_set_error(err, "accept: %s", strerror(errno));
                return -1;
            }
        }
        break;
    }
    return fd;
}

int anet_tcp_accept(char *err, int s, char *ip, size_t ip_len, int *port, int *family) {
    int fd;
    struct sockaddr_storage sa;
    socklen_t salen = sizeof(sa);

    fd = anet_generic_accept(err, s, (struct sockaddr *)&sa, &salen);
    if (fd == -1) {
        return ANET_ERR;
    }

    *family = sa.ss_family;

    if (sa.ss_family == AF_INET) {
        struct sockaddr_in *s = (struct sockaddr_in *)&sa;
        if (ip) inet_ntop(AF_INET, (void *)&(s->sin_addr), ip, ip_len);
        if (port) *port = ntohs(s->sin_port);
    } else {
        struct sockaddr_in6 *s = (struct sockaddr_in6 *)&sa;
        if (ip) inet_ntop(AF_INET6, (void *)&(s->sin6_addr), ip, ip_len);
        if (port) *port = ntohs(s->sin6_port);
    }

    return fd;
}

int anet_keep_alive(char *err, int fd, int interval) {
    int val = 1;

    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val)) == -1) {
        anet_set_error(err, "setsockopt SO_KEEPALIVE: %s", strerror(errno));
        return ANET_ERR;
    }

#ifdef __linux__
    /* Default settings are more or less garbage, with the keepalive time
     * set to 7200 by default on Linux. Modify settings to make the feature
     * actually useful. */

    /* Send first probe after interval. */
    val = interval;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &val, sizeof(val)) < 0) {
        anet_set_error(err, "setsockopt TCP_KEEPIDLE: %s\n", strerror(errno));
        return ANET_ERR;
    }

    /* Send next probes after the specified interval. Note that we set the
     * delay as interval / 3, as we send three probes before detecting
     * an error (see the next setsockopt call). */
    val = interval / 3;
    if (val == 0) val = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &val, sizeof(val)) < 0) {
        anet_set_error(err, "setsockopt TCP_KEEPINTVL: %s\n", strerror(errno));
        return ANET_ERR;
    }

    /* Consider the socket in error state after three we send three ACK
     * probes without getting a reply. */
    val = 3;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &val, sizeof(val)) < 0) {
        anet_set_error(err, "setsockopt TCP_KEEPCNT: %s\n", strerror(errno));
        return ANET_ERR;
    }
#else
    ((void)interval); /* Avoid unused var warning for non Linux systems. */
#endif

    return ANET_OK;
}

int anet_set_tcp_no_delay(char *err, int fd, int val) {
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) == -1) {
        anet_set_error(err, "setsockopt TCP_NODELAY: %s", strerror(errno));
        return ANET_ERR;
    }
    return ANET_OK;
}

int anet_tcp_connect(char *err, const char *addr, int port, bool is_block,
                     struct sockaddr *saddr, size_t *saddrlen) {
    int s = ANET_ERR, rv;
    char portstr[6];
    struct addrinfo hints, *servinfo, *p;

    snprintf(portstr, sizeof(portstr), "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(addr, portstr, &hints, &servinfo)) != 0) {
        anet_set_error(err, "%s", gai_strerror(rv));
        return ANET_ERR;
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {
        /* Try to create the socket and to connect it.
         * If we fail in the socket() call, or on connect(), we retry with
         * the next entry in servinfo. */
        if ((s = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            continue;
        }

        if (anet_set_reuse_addr(err, s) == ANET_ERR) {
            goto error;
        }

        if (!is_block && anet_no_block(err, s) != ANET_OK) {
            goto error;
        }

        if (connect(s, p->ai_addr, p->ai_addrlen) == -1) {
            /* If the socket is non-blocking, it is ok for connect() to
             * return an EINPROGRESS error here. */
            if (errno == EINPROGRESS && !is_block)
                goto end;
            close(s);
            s = ANET_ERR;
            continue;
        }

        /* If we ended an iteration of the for loop without errors, we
         * have a connected socket. Let's return to the caller. */
        goto end;
    }

error:
    if (s != ANET_ERR) {
        close(s);
        s = ANET_ERR;
    }

end:
    if (saddr && saddrlen) {
        *saddrlen = p->ai_addrlen;
        memcpy(saddr, p->ai_addr, p->ai_addrlen);
    }
    freeaddrinfo(servinfo);
    return s;
}

int anet_check_connect_done(int fd, struct sockaddr *saddr, size_t saddr_len, bool &completed) {
    int rc = connect(fd, saddr, saddr_len);
    if (rc == 0) {
        completed = true;
        return ANET_OK;
    }
    switch (errno) {
        case EISCONN:
            completed = true;
            return ANET_OK;
        case EALREADY:
        case EINPROGRESS:
        case EWOULDBLOCK:
            completed = false;
            return ANET_OK;
        default:
            return ANET_ERR;
    }
}

}  // namespace kim