// ngx_channel.c

#include "chanel.h"

#include <string.h>
#include <sys/socket.h>

namespace kim {

#define LOG_FORMAT(level, args...) \
    logger->log_data(__FILE__, __LINE__, __FUNCTION__, level, ##args);
#define LOG_ERROR(args...) LOG_FORMAT((Log::LL_ERR), ##args)
#define LOG_DEBUG(args...) LOG_FORMAT((Log::LL_DEBUG), ##args)
#define LOG_TRACE(args...) LOG_FORMAT((Log::LL_TRACE), ##args)

int write_channel(int fd, channel_t* ch, size_t size, Log* logger) {
    LOG_TRACE("write to channel, fd: %d, family: %d, codec: %d",
              ch->fd, ch->family, ch->codec);
    ssize_t n;
    struct iovec iov[1];
    struct msghdr msg;
    int err = 0;

    union {
        struct cmsghdr cm;
        char space[CMSG_SPACE(sizeof(int))];
    } cmsg;

    if (ch->fd == -1) {
        msg.msg_control = NULL;
        msg.msg_controllen = 0;
    } else {
        msg.msg_control = (caddr_t)&cmsg;
        msg.msg_controllen = sizeof(cmsg);

        memset(&cmsg, 0, sizeof(cmsg));

        cmsg.cm.cmsg_len = CMSG_LEN(sizeof(int));
        cmsg.cm.cmsg_level = SOL_SOCKET;
        cmsg.cm.cmsg_type = SCM_RIGHTS;

        /*
         * We have to use ngx_memcpy() instead of simple
         *   *(int *) CMSG_DATA(&cmsg.cm) = ch->fd;
         * because some gcc 4.4 with -O2/3/s optimization issues the warning:
         *   dereferencing type-punned pointer will break strict-aliasing rules
         *
         * Fortunately, gcc with -O1 compiles this ngx_memcpy()
         * in the same simple assignment as in the code above
         * 
         * ngx_memcpy(CMSG_DATA(&cmsg.cm), &ch->fd, sizeof(int));
         */
        *(int*)CMSG_DATA(&cmsg.cm) = ch->fd;
    }

    msg.msg_flags = 0;

    iov[0].iov_base = (char*)ch;
    iov[0].iov_len = size;

    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    n = sendmsg(fd, &msg, 0);

    if (n == -1) {
        err = errno;
        if (err == EAGAIN) {
            LOG_DEBUG("wait to sendmsg again! err: %d, error: %s",
                      errno, strerror(errno));
            return err;
        }
        LOG_ERROR("sendmsg() failed! err: %d, error: %s",
                  errno, strerror(errno));
        return -1;
    }
    return 0;
}

int read_channel(int fd, channel_t* ch, size_t size, Log* logger) {
    LOG_TRACE("read from channel, channel fd: %d", fd);
    ssize_t n;
    int err = 0;
    struct iovec iov[1];
    struct msghdr msg;

    union {
        struct cmsghdr cm;
        char space[CMSG_SPACE(sizeof(int))];
    } cmsg;

    iov[0].iov_base = (char*)ch;
    iov[0].iov_len = size;

    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    msg.msg_control = (caddr_t)&cmsg;
    msg.msg_controllen = sizeof(cmsg);

    n = recvmsg(fd, &msg, 0);

    if (n == -1) {
        err = errno;
        if (err == EAGAIN) {
            return err;
        }
        LOG_ERROR("recvmsg() failed!");
        return -1;
    }

    if (n == 0) {
        LOG_ERROR("rrecvmsg() returned zero! err: %d, error: %s",
                  errno, strerror(errno));
        return -1;
    }

    if ((size_t)n < sizeof(channel_t)) {
        LOG_ERROR(
            "recvmsg() returned not enough data : %z, err: %d, error: %s",
            n, errno, strerror(errno));
        return -1;
    }

    if (cmsg.cm.cmsg_len < (socklen_t)CMSG_LEN(sizeof(int))) {
        LOG_ERROR(
            "recvmsg() returned too small ancillary data. err: %d, error: %s",
            errno, strerror(errno));
        return -1;
    }

    if (cmsg.cm.cmsg_level != SOL_SOCKET || cmsg.cm.cmsg_type != SCM_RIGHTS) {
        LOG_ERROR(
            "recvmsg() returned invalid ancillary data "
            "level %d or type %d, err: %d, error: %s",
            cmsg.cm.cmsg_level, cmsg.cm.cmsg_type,
            errno, strerror(errno));
        return -1;
    }

    ch->fd = *(int*)CMSG_DATA(&cmsg.cm);

    if (msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) {
        LOG_ERROR("recvmsg() truncated data, err: %d, error: %s",
                  errno, strerror(errno));
        return -1;
    }

    return 0;
}

}  // namespace kim