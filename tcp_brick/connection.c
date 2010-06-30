/**
 * This module manages the connection with the tcp_brick server. It will try to
 * connect to it on the first actual send operation, not on initialisation.
 */

#include "tcp_brick/connection.h"

#include <arpa/inet.h>
#include <attr/xattr.h>
#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <stdlib.h>
#include <sys/socket.h>

#include "kfs.h"
#include "kfs_misc.h"
#include "tcp_brick/kfs_brick_tcp.h"
#include "tcp_brick/tcp_brick.h"

/** Maximum number of subsequent reconnect retries. */
static const unsigned int MAX_RETRIES = 10;
/** Number of seconds to wait after failed attempt before reconnecting. */
static const unsigned int RETRY_DELAY = 3;
static struct kfs_brick_tcp_arg myconf;
static int cached_sockfd = 0;

/**
 * Returns true if a previously executed socket-operation-related syscall that
 * failed did so because of a surmountable error, a disconnection of sorts.
 */
static unsigned int
recoverable_error(int errno_)
{
    switch (errno_) {
    case ECONNREFUSED:
    case ENOTCONN:
    case EINTR:
    case EADDRNOTAVAIL:
    case EINPROGRESS:
    case EISCONN:
    case ENETUNREACH:
    case ETIMEDOUT:
    case EADDRINUSE:
    case ENETDOWN:
        return 1;
        break;
    default:
        return 0;
        break;
    }
}

/**
 * Try to send given buffer over given socket. Blocks until entire message is
 * sent. Returns -1 on critical failure, +1 on recoverable connection failure
 * and 0 on success.
 */
static int
kfs_send(int sockfd, const char *buf, size_t buflen)
{
    size_t done = 0;
    ssize_t n = 0;

    KFS_ENTER();

    done = 0;
    while (done != buflen) {
        n = send(sockfd, buf + done, buflen - done, 0);
        switch (n) {
        case -1:
            if (recoverable_error(errno)) {
                KFS_RETURN(1);
            } else {
                KFS_ERROR("send: %s", strerror(errno));
                KFS_RETURN(-1);
            }
            break;
        case 0:
            KFS_DEBUG("Disconnected from server while sending data.");
            KFS_RETURN(1);
            break;
        }
        done += n;
        KFS_ASSERT(done <= buflen);
    }

    /* Control never reaches this point. */
    KFS_RETURN(0);
}

/**
 * Blocks until the entire buffer is filled. This means the size of the message
 * must be known before reception. Often determined by the header of the
 * actual message, this requires splitting everything in two: the header (known
 * fixed size, one recv() call) and the body (now also known size, another
 * recv() call). Always requiring at least two recv() calls may prove
 * inefficient for small messages that easily fit inside a local buffer. The
 * alternative is keeping a circular buffer that receives whatever it can take
 * and returns what is requested when it has enough. The current implementation
 * does not do that.
 *
 * Returns -1 on critical failure, +1 on recoverable connection failure and 0 on
 * success.
 */
static int
kfs_recv(int sockfd, char *buf, size_t buflen)
{
    ssize_t n = 0;

    KFS_ENTER();

    if (buflen > SSIZE_MAX) {
        /* TODO: split up in multiple smaller buffers instead. */
        KFS_ERROR("Requested buffer size too large.");
        KFS_RETURN(-1);
    } else if (buflen == 0) {
        KFS_RETURN(0);
    }
    for (;;) {
        n = recv(sockfd, buf, buflen, MSG_WAITALL);
        switch (n) {
        case -1:
            if (recoverable_error(errno)) {
                KFS_RETURN(1);
            } else {
                KFS_ERROR("recv: %s", strerror(errno));
                KFS_RETURN(-1);
            }
            break;
        case 0:
            KFS_DEBUG("Disconnected from server while receiving data.");
            KFS_RETURN(1);
            break;
        default:
            KFS_ASSERT(n == buflen);
            KFS_RETURN(0);
            break;
        }
    }

    /* Control never reaches this point. */
    KFS_RETURN(-1);
}

/**
 * Send given send buffer to the server and then receive indicated amount of
 * bytes into given receive buffer (synchronous). Returns 0 on succes, -1 on
 * critical failure and +1 on recoverable failure.
 */
static int
kfs_sendrecv(int sockfd, char *recvbuf, size_t recvbufsize,
             const char *sendbuf, size_t sendbufsize)
{
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(recvbuf != NULL && sendbuf != NULL);
    ret = kfs_send(sockfd, sendbuf, sendbufsize);
    if (ret == 0) {
        ret = kfs_recv(sockfd, recvbuf, recvbufsize);
    }

    KFS_RETURN(ret);
}

/**
 * Send the start-of-protocol over given socket and check if it comes in as
 * well. Returns -1 on critical failure, +1 on recoverable connection failure
 * and 0 on success.
 */
static int
sendrecv_sop(int sockfd)
{
    /* Trailing '\0'-byte unnecessary. */
    const size_t BUFSIZE = NUMELEM(SOP_STRING) - 1;
    int ret = 0;
    char buf[BUFSIZE];

    KFS_ENTER();

    ret = kfs_sendrecv(sockfd, buf, BUFSIZE, SOP_STRING, BUFSIZE);
    if (ret == 0 && strncmp(SOP_STRING, buf, BUFSIZE) != 0) {
        KFS_ERROR("Received invalid start of protocol.");
        KFS_RETURN(-1);
    }

    KFS_RETURN(ret);
}

/**
 * Connect to the server specified in the configuration.  Returns the socket for
 * the connection with the server on success. If one of the tested sockets had
 * potential but could not be opened due to recoverable errors, -2 is returned.
 * If all potential sockets caused critical errors, -1 is returned.
 */
static int
connect_to_server(const struct kfs_brick_tcp_arg *conf)
{
    struct addrinfo hints;
    struct addrinfo *servinfo = NULL;
    struct addrinfo *p = NULL;
    /* Set to true of one socket fails due to a recoverable error. */
    unsigned int atleastonegood = 0;
    int sockfd = 0;
    int ret = 0;

    KFS_ENTER();

    KFS_INFO("Connecting to %s:%s.", conf->hostname, conf->port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    ret = getaddrinfo(conf->hostname, conf->port, &hints, &servinfo);
    if (ret != 0) {
        KFS_ERROR("getaddrinfo: %s", gai_strerror(ret));
        KFS_RETURN(-1);
    }
    /* Try to get connected to the host by going through all possibilities. */
    for (p = servinfo; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) {
            KFS_INFO("socket: %s", strerror(errno));
            continue;
        }
        ret = connect(sockfd, p->ai_addr, p->ai_addrlen);
        if (ret == -1) {
            /* Order is important: KFS_INFO() might reset errno. */
            atleastonegood |= recoverable_error(errno);
            KFS_INFO("connect: %s", strerror(errno));
            continue;
        }
        /* Found a good socket. */
        break;
    }
    /* TODO: Does freeaddrinfo(NULL) fail? If not, this check is spurious. */
    if (servinfo != NULL) {
        freeaddrinfo(servinfo);
    }
    if (p == NULL) {
        KFS_ERROR("Could not connect to %s:%s", conf->hostname, conf->port);
        KFS_RETURN(-1 - atleastonegood);
    }

    KFS_RETURN(sockfd);
}

/**
 * Try to reconnect to server until either success (return the socket) or
 * critical failure (return -1). Argument 1 is the old socket, argument 2 is a
 * pointer to the maximum number of times this routine can retry to get a
 * connection if an attempt fails, argument 3 is the configuration for the
 * connection to the new server. Argument 1 is ignored if it equals -1. Argument
 * 2 will be decremented by one upon every retry, until it reaches 0, after
 * which a next retry will cause this function to exit with -1 instead.
 */
static int
refresh_socket(int sockfd, unsigned int *retries,
               const struct kfs_brick_tcp_arg *conf)
{
    int ret = 0;

    KFS_ENTER();

    for (;;) {
        if (sockfd >= 0) {
            ret = shutdown(sockfd, SHUT_RDWR);
            if (ret == -1 && errno != ENOTCONN) {
                KFS_ERROR("shutdown: %s", strerror(errno));
                KFS_RETURN(-1);
            }
        }
        kfs_sleep(RETRY_DELAY);
        sockfd = connect_to_server(conf);
        if (sockfd != -2) {
            break;
        } else if (*retries == 0) {
            sockfd = -1;
            break;
        } else {
            *retries -= 1;
        }
    }

    KFS_RETURN(sockfd);
}

/**
 * Send given operation to the server and wait for its reply. Returns 0 on
 * succes, -1 on unrecoverable failure.
 */
int
do_operation(char *resbuf, size_t resbufsize,
             const char *operbuf, size_t operbufsize)
{
    int sockfd = 0;
    int ret = 0;
    unsigned int retries = MAX_RETRIES;

    KFS_ENTER();

    KFS_ASSERT(resbuf != NULL && operbuf != NULL);
    sockfd = cached_sockfd;
    retries = 0;
    for (;;) {
        ret = kfs_sendrecv(sockfd, resbuf, resbufsize, operbuf, operbufsize);
        if (ret != 1) {
            /* Quit if an unrecoverable error occurs and on success. */
            break;
        }
        /*
         * Recursion would be much more elegant but if connection never succeeds
         * it will eat the stack and eventually cause a segfault.
         */
        do {
            if (retries == 0) {
                KFS_RETURN(-1);
            }
            retries -= 1;
            sockfd = refresh_socket(sockfd, &retries, &myconf);
            if (sockfd == -1) {
                KFS_RETURN(-1);
            }
            ret = sendrecv_sop(sockfd);
            if (ret == -1) {
                KFS_RETURN(-1);
            }
        } while (ret == 1);
    }
    cached_sockfd = sockfd;

    KFS_RETURN(ret);
}

/**
 * Initialise the module by storing a local copy of the configuration.
 */
int
init_connection(const struct kfs_brick_tcp_arg *conf)
{
    unsigned int i = 0;

    KFS_ENTER();

    myconf = *conf;
    cached_sockfd = connect_to_server(conf);
    for (i = MAX_RETRIES; cached_sockfd == -2 && i > 0; i--) {
        cached_sockfd = refresh_socket(-1, &i, conf);
    }
    switch (cached_sockfd) {
    case -2:
    case -1:
        KFS_RETURN(-1);
        break;
    default:
        KFS_RETURN(0);
        break;
    }
}
