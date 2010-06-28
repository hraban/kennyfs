/**
 * KennyFS backend forwarding everything to a kennyfs server over TCP.
 */

#define FUSE_USE_VERSION 26
/* Macro is necessary to get fstatat(). */
#define _ATFILE_SOURCE
/* Macro is necessary to get pread(). */
#define _XOPEN_SOURCE 500
/* Macro is necessary to get dirfd(). */
#define _BSD_SOURCE

/* <attr/xattr.h> needs this header. */
#include <sys/types.h>

#include <arpa/inet.h>
#include <attr/xattr.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <limits.h>
#include <netdb.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <utime.h>

#include "kfs.h"
#include "kfs_api.h"
#include "kfs_misc.h"
#include "tcp_brick/tcp_brick.h"

/* Many functions use this macro in allocating a buffer on the stack to build a
 * full pathname. When it is exceeded more memory is automatically allocated on
 * the heap. Happens with kfs_bufstrcat().
 */
#define PATHBUF_SIZE 256

/*
 * Types.
 */

struct kenny_fh {
    DIR *dir;
    int fd;
};

struct kfs_brick_tcp_arg {
    char port[7];
    char *hostname;
};


/*
 * Globals and statics.
 */

static struct kfs_brick_tcp_arg *myconf;

static const struct fuse_operations kenny_oper = {
};

/**
 * Send the start-of-protocol over given socket and check if it comes in as
 * well. Returns -1 on failure, 0 on success. Never closes the socket.
 */
static int
sendrecv_sop(int sockfd)
{
    /* Trailing '\0'-byte unnecessary. */
    const size_t BUFSIZE = NUMELEM(SOP_STRING) - 1;
    size_t done = 0;
    ssize_t n = 0;
    char buf[BUFSIZE + 1]; /* Cheat in an extra byte for debugging later on. */

    KFS_ENTER();

    /*
     * Receive SOP first.
     */
    KFS_ASSERT(BUFSIZE <= SSIZE_MAX);
    KFS_ASSERT(BUFSIZE > 0);
    done = 0;
    while (done < BUFSIZE) {
        n = read(sockfd, buf, BUFSIZE - done);
        switch (n) {
        case -1:
            KFS_ERROR("read: %s", strerror(errno));
            KFS_RETURN(-1);
            break;
        case 0:
            KFS_ERROR("Connection closed while reading start of protocol.");
            KFS_RETURN(-1);
            break;
        default:
            done += n;
            break;
        }
    }
    KFS_ASSERT(done <= BUFSIZE);
    if (strncmp(SOP_STRING, buf, BUFSIZE) != 0) {
        buf[BUFSIZE] = '\0';
        KFS_ERROR("Received invalid start of protocol: '%s'.", buf);
        KFS_RETURN(-1);
    }
    /*
     * Send the SOP.
     */
    done = 0;
    while (done < BUFSIZE) {
        n = write(sockfd, SOP_STRING, BUFSIZE - done);
        if (n == -1) {
            KFS_ERROR("write: %s", strerror(errno));
            KFS_RETURN(-1);
        }
        done += n;
    }

    KFS_RETURN(0);
}

/**
 * Connect to the server specified in the configuration. The argument is the
 * same pointer as the global variable myconf but explicitly passing it improves
 * readability.
 */
static int
connect_to_server(const struct kfs_brick_tcp_arg *conf)
{
    struct addrinfo hints;
    struct addrinfo *servinfo = NULL;
    struct addrinfo *p = NULL;
    int sockfd = 0;
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(conf == myconf);
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
            KFS_INFO("connect: %s", strerror(errno));
            continue;
        }
        /* Found a good socket. */
        break;
    }
    if (p == NULL) {
        KFS_ERROR("Could not connect to %s:%s.", conf->hostname, conf->port);
        KFS_RETURN(-1);
    }
    freeaddrinfo(servinfo);
    ret = sendrecv_sop(sockfd);

    KFS_RETURN(ret);
}

/**
 * Create a new arg struct, specific for the TCP brick.
 * TODO: could use a better name.
 */
static struct kfs_brick_tcp_arg *
private_makearg(char *hostname, char *port)
{
    struct kfs_brick_tcp_arg *arg = NULL;

    KFS_ENTER();

    KFS_ASSERT(hostname != NULL && port != NULL);
    /*
     * TODO: this may be compiled out, which leaves a buffer overflow
     * vulnerability.
     */
    KFS_ASSERT(strlen(port) < 7);
    arg = KFS_MALLOC(sizeof(*arg));
    if (arg != NULL) {
        strcpy(arg->port, port);
        arg->hostname = kfs_strcpy(hostname);
        if (arg->hostname == NULL) {
            arg = KFS_FREE(arg);
            arg = NULL;
        }
    }

    KFS_RETURN(arg);
}

/**
 * Free a TCP brick arg struct.
 */
static struct kfs_brick_tcp_arg *
private_delarg(struct kfs_brick_tcp_arg *arg)
{
    KFS_ENTER();

    KFS_ASSERT(arg != NULL && arg->hostname != NULL);
    arg->hostname = KFS_FREE(arg->hostname);
    arg = KFS_FREE(arg);

    KFS_RETURN(arg);
}

/**
 * Unserialize an argument.
 */
static struct kfs_brick_tcp_arg *
kfs_brick_tcp_char2arg(char *buf, size_t len)
{
    struct kfs_brick_tcp_arg *arg = NULL;
    char *delim1 = NULL;

    KFS_ENTER();

    KFS_ASSERT(buf != NULL && len > 1);
    KFS_ASSERT(buf[len - 1] == '\0');
    /* The first '\0'-byte, separating hostname from port number. */
    delim1 = strchr(buf, '\0');
    /* There must be more than one '\0'-byte. */
    KFS_ASSERT(delim1 != &(buf[len - 1]));
    arg = private_makearg(buf, delim1 + 1);

    KFS_RETURN(arg);
}

/**
 * Serialize an argument to a character array, which must, eventually, be freed.
 * Returns the size of the buffer on success, sets the buffer to NULL on
 * failure.
 */
static size_t
kfs_brick_tcp_arg2char(char **buf, const struct kfs_brick_tcp_arg *arg)
{
    size_t len_port = 0;
    size_t len_hostname = 0;
    size_t len_total = 0;

    KFS_ENTER();

    KFS_ASSERT(arg != NULL && buf != NULL);
    KFS_ASSERT(arg->port != NULL && arg->hostname != NULL);
    len_port = strlen(arg->port) + 1; /* *Including* the '\0' byte. */
    KFS_ASSERT(1 < len_port && len_port < 7);
    len_hostname = strlen(arg->hostname) + 1;
    KFS_ASSERT(len_hostname > 1);
    len_total = len_port + len_hostname; /* Two '\0'-bytes. */
    *buf = KFS_MALLOC(len_total);
    if (*buf != NULL) {
        memcpy(*buf, arg->hostname, len_hostname);
        memcpy(*buf + len_hostname, arg->port, len_port);
    }

    KFS_RETURN(len_total);
}

/**
 * Initialize a new argument struct. Returns NULL on error, pointer to struct on
 * success. That pointer must eventually be freed. This function is useful for
 * other bricks / frontends to create arguments compatible with this brick.
 */
static struct kfs_brick_arg *
kenny_makearg(char *hostname, char *port)
{
    ssize_t serial_size = 0;
    char *serial_buf = NULL;
    struct kfs_brick_tcp_arg *arg_specific = NULL;
    struct kfs_brick_arg *arg_generic = NULL;

    KFS_ENTER();

    KFS_ASSERT(hostname != NULL && port != NULL);
    arg_generic = NULL;
    /* Create a TCP block-specific argument. */
    arg_specific = private_makearg(hostname, port);
    if (arg_specific == NULL) {
        KFS_RETURN(NULL);
    }
    /* Transform that into a generic arg (serialization). */
    serial_size = kfs_brick_tcp_arg2char(&serial_buf, arg_specific);
    arg_specific = private_delarg(arg_specific);
    if (serial_buf == NULL) {
        KFS_RETURN(NULL);
    }
    /*
     * Wrap serialised argument into generic struct.
     */
    arg_generic = kfs_brick_makearg(serial_buf, serial_size);
    if (arg_generic == NULL) {
        serial_buf = KFS_FREE(serial_buf);
        KFS_RETURN(NULL);
    }

    KFS_RETURN(arg_generic);
}

/**
 * Global initialization.
 */
static int
kenny_init(struct kfs_brick_arg *generic)
{
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(generic != NULL);
    KFS_ASSERT(generic->payload != NULL && generic->payload_size > 0);
    myconf = kfs_brick_tcp_char2arg(generic->payload, generic->payload_size);
    if (myconf == NULL) {
        KFS_ERROR("Initializing TCP brick failed.");
        KFS_RETURN(-1);
    }
    ret = connect_to_server(myconf);
    if (ret == -1) {
        myconf = private_delarg(myconf);
    }

    KFS_RETURN(ret);
}

/*
 * Get the backend interface.
 */
static const struct fuse_operations *
kenny_getfuncs(void)
{
    KFS_ENTER();

    KFS_RETURN(&kenny_oper);
}

/**
 * Global cleanup.
 */
static void
kenny_halt(void)
{
    KFS_ENTER();

    myconf = private_delarg(myconf);

    KFS_RETURN();
}

static const struct kfs_brick_api kenny_api = {
    .makearg = kenny_makearg,
    .init = kenny_init,
    .getfuncs = kenny_getfuncs,
    .halt = kenny_halt,
};

/*
 * Public functions.
 */

/**
 * Get the KennyFS API: initialiser, FUSE API getter and cleaner. This function
 * has the generic KennyFS backend name expected by frontends. Do not put this
 * in the header-file but extract it with dynamic linking.
 */
const struct kfs_brick_api *
kfs_brick_getapi(void)
{
    KFS_ENTER();

    KFS_RETURN(&kenny_api);
}
