/**
 * KennyFS network server. Listens for TCP connections and serves kennyfs
 * network clients. Note that there is NO authentication and NO encryption, so
 * please only start this in a trusted environment. All network operations are
 * non-blocking but all operations are blocking (i.e.: one slow client will not
 * clog the server but one client requesting something from a slow drive will).
 *
 * The protocol is as follows:
 *
 * - Both send the same SOP (start of protocol) string to verify protocol
 *   conformance.
 * - Client sends an operation, server processes it and replies with the result.
 *
 * An operation is built up like this:
 *
 * - Size of the serialised operation as a uint32_t (4 bytes).
 * - ID of the operation as a uint16_t (2 bytes).
 * - Serialized operation (size - 2 bytes).
 */

#include "server.h"

#define FUSE_USE_VERSION 26

#include <dlfcn.h>
#include <errno.h>
#include <fuse.h>
#include <fuse_opt.h>
#include <netdb.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "kfs.h"
#include "kfs_api.h"
#include "kfs_misc.h"
#include "kennyfs-network.h"
#include "network-server/server.h"
#include "network-server/handlers.h"

/** The size of per-client read and write buffers. */
#define BUF_LEN 10

/**
 * Configuration variables.
 */
struct kenny_conf {
    char *path;
    char *brick;
    char *port;
};

/** Temporary buffer for reading and writing to clients. */
static char tmp_buf[BUF_LEN];
/** All connected clients. */
static client_t clients = NULL;
/** Handlers for operations. */
static const handler_t *handlers = NULL;

/**
 * Runtime integrity check of a client struct. NOP if debugging is disabled.
 */
static void
verify_client(client_t c)
{
    KFS_ASSERT(c != NULL);
    KFS_ASSERT(c->readbuf_start != NULL);
    KFS_ASSERT(c->readbuf_end != NULL);
    KFS_ASSERT(c->readbuf_end - c->readbuf_start == BUF_LEN);
    KFS_ASSERT(c->readbuf_used <= BUF_LEN);
    KFS_ASSERT(c->readbuf_head >= c->readbuf_start);
    KFS_ASSERT(c->readbuf_head < c->readbuf_end);
    KFS_ASSERT(c->writebuf_start != NULL);
    KFS_ASSERT(c->writebuf_end != NULL);
    KFS_ASSERT(c->writebuf_end - c->writebuf_start == BUF_LEN);
    KFS_ASSERT(c->writebuf_used <= BUF_LEN);
    KFS_ASSERT(c->writebuf_head >= c->writebuf_start);
    KFS_ASSERT(c->writebuf_head < c->writebuf_end);
    KFS_ASSERT(c->got_sop == 0 || c->got_sop == 1);
    KFS_ASSERT((c->prev != NULL) ^ (clients == c));
    KFS_ASSERT(clients == NULL || clients->prev == NULL);
};

/**
 * Get a string with given number of bytes from the read buffer. Allocates a new
 * string and returns it, or NULL if not enough bytes are available in the read
 * buffer.
 */
static char *
read_readbuffer(client_t c, size_t n)
{
    char *result = NULL;
    size_t contig = 0;

    KFS_ENTER();

    verify_client(c);
    if (n > c->readbuf_used) {
        KFS_RETURN(NULL);
    }
    result = KFS_MALLOC(n);
    contig = c->readbuf_end - c->readbuf_head;
    if (contig > n) {
        result = memcpy(result, c->readbuf_head, n);
        c->readbuf_head += n;
    } else {
        memcpy(result, c->readbuf_head, contig);
        memcpy(result + contig, c->readbuf_start, n - contig);
        c->readbuf_head = c->readbuf_start + (n - contig);
    }
    c->readbuf_used -= n;

    KFS_RETURN(result);
}

/**
 * Process a serialized operation for given client. Returns -1 if the serialized
 * object is corrupted.
 */
static int
process_operation(client_t c, const char *rawop, size_t opsize)
{
    uint16_t net_i = 0;
    uint_t opid = 0;
    ssize_t size = 0;
    int ret = 0;
    handler_t handler = NULL;

    KFS_ENTER();

    size = sizeof(uint16_t);
    KFS_ASSERT(size == 2);
    if (opsize < size) {
        KFS_RETURN(-1);
    }
    memcpy(&net_i, rawop, size);
    opid = ntohs(net_i);
    if (opid >= KFS_OPID_MAX_) {
        KFS_RETURN(-1);
    }
    KFS_DEBUG("Processing operation %u.", opid);
    /* TODO: Process. */
    rawop += size;
    opsize -= size;
    handler = handlers[opid];
    ret = 0;
    if (handler != NULL) {
        ret = handler(c, rawop, opsize);
    }

    KFS_RETURN(ret);
}

/**
 * If a new operation is completely in the receive buffer, process it.
 */
static int
process_readbuffer(client_t c)
{
    char *raw = NULL;
    uint32_t net_i = 0;
    uint32_t opsize = 0;
    size_t size = 0;
    int ret = 0;

    KFS_ENTER();

    verify_client(c);
    if (!c->got_sop) {
        /* Wait for start of protocol first to check client validity. */
        size = strlen(SOP_STRING);
        raw = read_readbuffer(c, size);
        if (raw == NULL) {
            KFS_RETURN(0);
        }
        ret = memcmp(SOP_STRING, raw, size);
        if (ret != 0) {
            /* Did not get proper start of protocol message from client. */
            /* TODO: Send back proper error message. */
            KFS_DEBUG("Received erroneous SOP from client.");
            KFS_RETURN(-1);
        }
        KFS_DEBUG("Received proper SOP from client.");
        c->got_sop = 1;
    }
    if (c->opsize == 0) {
        /* No operation pending: get the size of the next one (four bytes). */
        KFS_ASSERT(sizeof(uint32_t) == 4);
        raw = read_readbuffer(c, sizeof(uint32_t));
        if (raw == NULL) {
            KFS_RETURN(0);
        }
        memcpy(&net_i, raw, sizeof(uint32_t));
        KFS_FREE(raw);
        opsize = ntohl(net_i);
        if (opsize > BUF_LEN) {
            /* TODO: Send back error instead of disconnecting. */
            KFS_ERROR("Incoming operation too big: %lu bytes?",
                    (unsigned long) opsize);
            KFS_RETURN(-1);
        }
        c->opsize = opsize;
    } else {
        /* Operation pending: see if it is now received in full. */
        raw = read_readbuffer(c, c->opsize);
        if (raw == NULL) {
            KFS_RETURN(0);
        }
        KFS_DEBUG("Received operation (%lu bytes)", (unsigned long) c->opsize);
        ret = process_operation(c, raw, c->opsize);
        KFS_FREE(raw);
        c->opsize = 0;
    }
    if (ret == 0) {
        /* Check the rest of the buffer. */
        ret = process_readbuffer(c);
    }

    KFS_RETURN(ret);
}

/**
 * Read data coming from this client, pending on the connection. Will execute a
 * blocking read(2) syscall, use select() to check for availability if wanted.
 * Returns -1 on failure, 0 if data was succesfully read, 1 if there is no more
 * buffer space available for this client (not fatal, just wait for a while and
 * retry later, hoping the client is patient as well), 2 if an EOF was
 * encountered.
 */
static int
read_pending(client_t c)
{
    char *p = NULL;
    ssize_t sysret = 0;
    size_t len = 0;
    int ret = 0;

    KFS_ENTER();

    verify_client(c);
    /* Free space in buffer. */
    len = BUF_LEN - c->readbuf_used;
    if (len == 0) {
        KFS_RETURN(1);
    }
    sysret = read(c->sockfd, tmp_buf, len);
    switch (sysret) {
    case -1:
        KFS_ERROR("read: %s", strerror(errno));
        KFS_RETURN(-1);
        break;
    case 0:
        KFS_RETURN(2);
        break;
    default:
        /* The address after the last used address. */
        p = c->readbuf_head + c->readbuf_used;
        /* The size of the next contiguous block. */
        len = c->readbuf_end - p;
        if (sysret <= len) {
            memcpy(p, tmp_buf, sysret);
        } else {
            /* Message does not fit at end: split it up. */
            memcpy(p, tmp_buf, len);
            memcpy(c->readbuf_start, tmp_buf + len, sysret - len);
        }
        c->readbuf_used += sysret;
        break;
    }
    verify_client(c);
    ret = process_readbuffer(c);

    KFS_RETURN(ret);
}

/**
 * Process write buffer of given client, send pending data (as much as
 * possible). Returns 0 on success, -1 on failure. Do not call if no pending
 * data is available.
 */
static int
write_pending(client_t c)
{
    size_t len = 0;
    ssize_t sysret = 0;
    char *buffer = NULL;

    KFS_ENTER();

    verify_client(c);
    KFS_ASSERT(c->writebuf_used != 0);
    len = c->writebuf_end - c->writebuf_head;
    if (len > c->writebuf_used) {
        buffer = c->writebuf_head;
    } else {
        /* Write buffer wraps around end: copy to temp and write() once. */
        memcpy(tmp_buf, c->writebuf_head, len);
        memcpy(tmp_buf + len, c->writebuf_start, c->writebuf_used - len);
        buffer = tmp_buf;
    }
    sysret = write(c->sockfd, buffer, c->writebuf_used);
    if (sysret == -1) {
        KFS_ERROR("write: %s", strerror(errno));
        KFS_RETURN(-1);
    }
    c->writebuf_used -= sysret;
    c->writebuf_head += sysret;
    if (c->writebuf_head >= c->writebuf_end) {
        c->writebuf_head -= BUF_LEN;
    }
    verify_client(c);

    KFS_RETURN(0);
}

/**
 * Send raw message (array of chars) to given client. Returns -1 if the buffer
 * is full, 0 on success. Puts the message in a buffer, actual sending happens
 * when the connection with the client is ready for it (and errors there are not
 * detected by this function).
 */
static int
send_msg(client_t c, const char *msg, size_t msglen)
{
    size_t len = 0;
    char *p = NULL;

    KFS_ENTER();

    KFS_ASSERT(msg != NULL);
    verify_client(c);
    if (msglen == 0) {
        KFS_RETURN(0);
    }
    /* Free space in buffer, total. */
    len = BUF_LEN - c->writebuf_used;
    if (msglen > len) {
        KFS_RETURN(-1);
    }
    /* Last used address + 1. */
    p = c->writebuf_head + c->writebuf_used;
    /* Length of free contiguous block. */
    len = c->writebuf_end - p;
    if (msglen <= len) {
        /* Contiguous free space fits message size. */
        memcpy(p, msg, msglen);
    } else {
        /* Split message up to fill contiguous space and continue at start. */
        memcpy(p, msg, len);
        memcpy(c->writebuf_start, msg + len, msglen - len);
    }
    c->writebuf_used += msglen;
    verify_client(c);

    KFS_RETURN(0);
}

/**
 * Send string (null-terminated array of chars). Does not include the appending
 * null byte.
 */
static int
send_string(client_t client, const char *string)
{
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(string != NULL);
    ret = send_msg(client, string, strlen(string));
    if (ret == -1) {
        KFS_DEBUG("Not enough buffer space to hold message '%s'.", string);
    }

    KFS_RETURN(ret);
}

/**
 * Close a socket, print a message on error. Returns -1 on failure, 0 on
 * success.
 */
static int
close_socket(int sockfd)
{
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(sockfd >= 0);
    /* TODO: shutdown(2) here, also? */
#if 0
    ret = shutdown(sockfd, SHUT_RDWR);
    if (ret == -1 && errno != ENOTCONN) {
        KFS_ERROR("shutdown: %s", strerror(errno));
    } else {
#endif
        ret = close(sockfd);
        if (ret == -1) {
            KFS_ERROR("close: %s", strerror(errno));
        }
#if 0
    }
#endif

    KFS_RETURN(ret);
}

/**
 * Handle disconnection of a client. Frees all allocated resources, including
 * the client struct.
 */
static int
disconnect_client(client_t c)
{
    int ret = 0;

    KFS_ENTER();

    verify_client(c);
    /* Remove from the list of connected clients. */
    if (c->prev != NULL) {
        c->prev->next = c->next;
    } else {
        clients = c->next;
    }
    if (c->next != NULL) {
        c->next->prev = c->prev;
    }
    c->readbuf_start = KFS_FREE(c->readbuf_start);
    c->writebuf_start = KFS_FREE(c->writebuf_start);
    ret = close_socket(c->sockfd);
    c = KFS_FREE(c);
    KFS_INFO("Disconnected client.");

    KFS_RETURN(ret);
}

/**
 * Process a new incoming connection.
 */
static int
connect_client(int sockfd)
{
    client_t c = NULL;
    int ret = 0;

    KFS_ENTER();

    c = KFS_MALLOC(sizeof(*c));
    if (c == NULL) {
        KFS_RETURN(-1);
    }
    c->readbuf_start = KFS_MALLOC(BUF_LEN);
    if (c->readbuf_start == NULL) {
        KFS_FREE(c);
        KFS_RETURN(-1);
    }
    c->readbuf_end = c->readbuf_start + BUF_LEN;
    c->readbuf_head = c->readbuf_start;
    c->readbuf_used = 0;
    c->writebuf_start = KFS_MALLOC(BUF_LEN);
    if (c->writebuf_start == NULL) {
        KFS_FREE(c->readbuf_start);
        KFS_FREE(c);
        KFS_RETURN(-1);
    }
    c->writebuf_end = c->writebuf_start + BUF_LEN;
    c->writebuf_head = c->writebuf_start;
    c->writebuf_used = 0;
    c->opsize = 0;
    c->sockfd = sockfd;
    /* Add the c to the global list of connected clients. */
    if (clients != NULL) {
        clients->prev = c;
    }
    c->next = clients;
    clients = c;
    c->prev = NULL;
    /* First characters sent are the start of protocol string. */
    ret = send_string(c, SOP_STRING);
    if (ret == -1) {
        /* Disconnection could also fail, but return value is already -1. */
        disconnect_client(c);
    } else {
        verify_client(c);
    }

    KFS_RETURN(ret);
}

/**
 * Create a socket that listens for incoming TCP connections on given port.
 */
static int
create_listen_socket(const char *port)
{
    const int yes = 1;
    struct addrinfo hints;
    struct addrinfo *ai_list = NULL;
    struct addrinfo *aip = NULL;
    int listen_sock = 0;
    int ret = 0;

    KFS_ENTER();

    /* Set requirements for listening address. */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; /* IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    ret = getaddrinfo(NULL, port, &hints, &ai_list);
    if (ret != 0) {
        KFS_ERROR("getaddrinfo: %s", gai_strerror(ret));
        KFS_RETURN(-1);
    }
    /* Create a listening socket. */
    for (aip = ai_list; aip != NULL; aip = aip->ai_next) {
        listen_sock = socket(aip->ai_family, aip->ai_socktype,
                aip->ai_protocol);
        if (listen_sock == -1) {
            continue;
        }
        ret = setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &yes,
                sizeof(yes));
        if (ret == -1) {
            close_socket(listen_sock); /* Errors are ignored. */
            continue;
        }
        ret = bind(listen_sock, aip->ai_addr, aip->ai_addrlen);
        if (ret == -1) {
            close_socket(listen_sock); /* Errors are ignored. */
            continue;
        }
        /* Socket was succesfully created and bound to. */
        break;
    }
    freeaddrinfo(ai_list);
    if (aip == NULL) {
        KFS_ERROR("Could not bind to port %s.", port);
        ret = -1;
    } else {
        /* Indicate willigness to accept incoming connections. */
        ret = listen(listen_sock, 10);
        if (ret == -1) {
            KFS_ERROR("listen: %s", strerror(errno));
        } else {
            ret = listen_sock;
        }
    }

    KFS_RETURN(ret);
}

/**
 * Listen for incoming connections and handle them.
 */
static int
run_daemon(char *port)
{
    fd_set allsocks;
    fd_set readset;
    fd_set writeset;
    client_t client = NULL;
    struct sockaddr_in client_address;
    size_t addrsize = 0;
    /* Socket listening for incoming connections. */
    int listen_sock = 0;
    int newsock = 0;
    int nfds = 0;
    int ret = 0;

    KFS_ENTER();

    ret = 0;
    FD_ZERO(&allsocks);
    FD_ZERO(&readset);
    FD_ZERO(&writeset);
    listen_sock = create_listen_socket(port);
    if (listen_sock == -1) {
        KFS_RETURN(-1);
    }
    FD_SET(listen_sock, &allsocks);
    nfds = listen_sock;
    for (;;) {
        /* Input all sockets to select(). */
        readset = allsocks;
        /* Only bother writing for clients that have something in buffer. */
        FD_ZERO(&writeset);
        for (client = clients; client != NULL; client = client->next) {
            if (client->writebuf_used != 0) {
                FD_SET(client->sockfd, &writeset);
            }
        }
        /* TODO: handle exceptfds? */
        ret = select(nfds + 1, &readset, &writeset, NULL, NULL);
        if (ret == -1) {
            KFS_ERROR("select: %s", strerror(errno));
            close_socket(listen_sock);
            break;
        }
        if (FD_ISSET(listen_sock, &readset)) {
            /* New incoming connection on listening socket. */
            addrsize = sizeof(client_address);
            memset(&client_address, 0, addrsize);
            newsock = accept(listen_sock,
                    (struct sockaddr *) &client_address, &addrsize);
            if (newsock == -1) {
                KFS_ERROR("accept: %s", strerror(errno));
                KFS_WARNING("Could not accept new connection.");
            } else {
                ret = connect_client(newsock);
                if (ret == 0) {
                    KFS_INFO("Succesfully accepted connection.");
                    FD_SET(newsock, &allsocks);
                    nfds = max(newsock, nfds);
                }
            }
        }
        /* Check all possible sockets to see if there is pending data. */
        for (client = clients; client != NULL; client = client->next) {
            ret = 0;
            if (FD_ISSET(client->sockfd, &readset)) {
                /* Pending data on existing connection. */
                KFS_DEBUG("Data available from client.");
                ret = read_pending(client);
                if (ret == 2) {
                    /* Client closing the connection. */
                    ret = -1;
                }
            }
            if (FD_ISSET(client->sockfd, &writeset)) {
                /* Writing is possible. */
                ret = write_pending(client);
            }
            if (ret == -1) {
                FD_CLR(client->sockfd, &allsocks);
                /* Error may occur while disconnecting client: ignore. */
                disconnect_client(client);
            }
        }
    }

    KFS_RETURN(ret);
}

/**
 * Start up the POSIX backend. Returns 0 on success, -1 on error.
 */
static int
prepare_posix_backend(struct kfs_brick_api *api, char *path)
{
    struct kfs_brick_arg *arg = NULL;
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(api != NULL && path != NULL);
    /* Prepare for initialization of the brick: construct its argument. */
    arg = api->makearg(path);
    if (arg == NULL) {
        KFS_ERROR("Preparing brick failed.");
        ret = -1;
    } else {
        /* Arguments ready: now initialize the brick. */
        ret = api->init(arg);
        if (ret != 0) {
            KFS_ERROR("Brick could not start: code %d.", ret);
            ret = -1;
        }
        /* Initialization is over, free the argument. */
        arg = kfs_brick_delarg(arg);
    }

    KFS_RETURN(ret);
}

int
main(int argc, char *argv[])
{
    struct kenny_conf conf;
    struct kfs_brick_api brick_api;
    int ret = 0;
    const struct fuse_operations *kenny_oper = NULL;
    kfs_brick_getapi_f brick_getapi_f = NULL;
    void *lib_handle = NULL;
    char *errorstr = NULL;

    KFS_ENTER();

    ret = 0;
    KFS_INFO("Starting KennyFS version %s.", KFS_VERSION);
    handlers = get_handlers();
    /* Parse the command line. */
    if (argc == 4) {
        conf.brick = argv[1];
        conf.path = argv[2];
        conf.port = argv[3];
    } else {
        KFS_ERROR("Error parsing commandline.");
        ret = -1;
    }
    if (ret == 0) {
        /* Load the brick. */
        lib_handle = dlopen(conf.brick, RTLD_NOW | RTLD_LOCAL);
        if (lib_handle != NULL) {
            brick_getapi_f = dlsym(lib_handle, "kfs_brick_getapi");
            if (brick_getapi_f != NULL) {
                brick_api = brick_getapi_f();
            } else {
                dlclose(lib_handle);
                ret = -1;
            }
        } else {
            ret = -1;
        }
        errorstr = dlerror();
        if (errorstr != NULL) {
            KFS_ERROR("Loading brick failed: %s", errorstr);
        }
    }
    if (ret == 0) {
        ret = prepare_posix_backend(&brick_api, conf.path);
        if (ret == 0) {
            /* Run the brick and start the network daemon. */
            kenny_oper = brick_api.getfuncs();
            init_handlers(kenny_oper);
            ret = run_daemon(conf.port);
            /* Clean everything up. */
            brick_api.halt();
        }
        dlclose(lib_handle);
    }
    if (ret == 0) {
        KFS_INFO("KennyFS exited succesfully.");
        exit(EXIT_SUCCESS);
    } else {
        KFS_WARNING("KennyFS exited with value %d.", ret);
        exit(EXIT_FAILURE);
    }

    KFS_RETURN(EXIT_FAILURE);
}
