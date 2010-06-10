/**
 * KennyFS command-line frontend. Lots of values are hard-coded (for now).
 */

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

#define KENNYFS_OPT(t, p, v) {t, offsetof(struct kenny_conf, p), v}

/**
 * Configuration variables.
 */
struct kenny_conf {
    char *path;
    char *brick;
    char *port;
};

/**
 * Node in a linked list of connected network clients.
 */
struct client_node {
    struct client_node *next;
    /** Start of the buffer for received, non-processed chars. */
    char readbuf_start[BUF_LEN];
    /** End of the buffer for received, non-processed chars. */
    char *readbuf_end;
    /** First of received chars that need processing. */
    char *readbuf_head;
    /** Last to-be-processed char, + 1 (ie: if head==tail all is processed). */
    char *readbuf_tail;
    /** Start of the buffer for chars that need to be sent to client.. */
    char writebuf_start[BUF_LEN];
    /** End of the buffer for chars that need to be sent to client. */
    char *writebuf_end;
    /** First of the chars that need to be sent to client. */
    char *writebuf_head;
    /** Last of the chars that need to be sent, + 1. */
    char *writebuf_tail;
    int sockfd;
};
typedef struct client_node *client_t;

/** The start of the protocol: sent whenever a new client connects. */
static const char SOP_STRING[] = "poep\n";
/** The size of per-client read and write buffers. */
static const size_t BUF_LEN = 1;
/** The first client in the list. This pointer almost never changes. */
static client_t clientlist_head = NULL;
/** The last client that connected. This pointer often changes. */
static client_t clientlist_tail = NULL;

/**
 * Send raw message (array of chars) to given client. Returns -1 on failure, 0
 * on success. Puts the message in a buffer, actual sending happens when the
 * connection with the client is ready for it (and errors there are not detected
 * by this function).
 */
static int
send_msg(client_t c, const char *msg, size_t msglen);
{
    int ret = 0;
    /* Space left in the write buffer. */
    size_t freebuflen = 0;
    /* Contigious space left in the write buffer. */
    size_t freecontig = 0;

    KFS_ENTER();

    KFS_ASSERT(msg != NULL);
    KFS_ASSERT(c != NULL);
    freebuflen = c->writebuf_tail - c->writebuf_head;
    if (c->writebuf_tail <= c->writebuf_head) {
        freebuflen = BUF_LEN - freebuflen;
    }
    if (msglen > freebuflen) {
        KFS_RETURN(-1);
    }
    freecontig = c->writebuf_end - c->writebuf_tail;
    if (msglen <= freecontig) {
        /* Contiguous free space fits message size. */
        memcpy(c->writebuf_tail, msg, msglen);
        c->writebuf_tail += msglen;
    } else {
        /* Split message up to fill contiguous space and continue at start. */
        memcpy(c->writebuf_tail, msg, freecontig);
        memcpy(c->writebuf_start, msg + freecontig, msglen - freecontig);
        c->writebuf_tail = c->writebuf_start + msglen - freecontig;
    }

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

    ret = send_msg(client, string, strlen(string));

    KFS_RETURN(ret);
}

/**
 * Process a new incoming connection.
 */
static int
new_client(int sockfd)
{
    client_t client = NULL;
    ssize_t syscallret = 0;
    int ret = 0;

    KFS_ENTER();

    client = KFS_MALLOC(sizeof(*client));
    if (client == NULL) {
        KFS_RETURN(-1);
    }
    client->readbuf_start = KFS_MALLOC(BUF_LEN);
    if (client->readbuf_start == NULL) {
        KFS_FREE(client);
        KFS_RETURN(-1);
    }
    client->readbuf_end = client->readbuf_start + BUF_LEN;
    client->readbuf_head = client->readbuf_start;
    client->readbuf_tail = client->readbuf_start;
    client->writebuf_start = KFS_MALLOC(BUF_LEN);
    if (client->writebuf_start == NULL) {
        KFS_FREE(client->readbuf_start);
        KFS_FREE(client);
        KFS_RETURN(-1);
    }
    client->writebuf_end = client->writebuf_start + BUF_LEN;
    client->writebuf_head = client->writebuf_start;
    client->writebuf_tail = client->writebuf_start;
    client->sockfd = sockfd;
    /* First characters sent are the start of protocol string. */
    ret = send_to_client(client, SOP_STRING);
    if (ret == -1) {
        KFS_FREE(client->writebuf_start);
        KFS_FREE(client->readbuf_start);
        KFS_FREE(client);
        KFS_RETURN(-1);
    }
    client->next = NULL;
    if (clientlist_tail == NULL) {
        KFS_ASSERT(clientlist_head == NULL);
        clientlist_head = client;
        clientlist_tail = client;
    } else {
        KFS_ASSERT(clientlist_head != NULL);
        clientlist_tail->next = client;
        clientlist_tail = next;
    }

    KFS_RETURN(0);
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
run_daemon(char *port, const struct fuse_operations *kenny_oper)
{
    (void) kenny_oper;

    fd_set allsocks;
    fd_set readset;
    struct sockaddr_in client_address;
    size_t addrsize = 0;
    /* Socket listening for incoming connections. */
    int listen_sock = 0;
    int newsock = 0;
    int nfds = 0;
    int ret = 0;
    int i = 0;

    KFS_ENTER();

    ret = 0;
    FD_ZERO(&allsocks);
    FD_ZERO(&readset);
    listen_sock = create_listen_socket(port);
    if (listen_sock == -1) {
        KFS_RETURN(-1);
    }
    FD_SET(listen_sock, &allsocks);
    nfds = listen_sock;
    for (;;) {
        /* Input all sockets to select(). */
        readset = allsocks;
        ret = select(nfds + 1, &readset, NULL, NULL, NULL);
        if (ret == -1) {
            KFS_ERROR("select: %s", strerror(errno));
            close_socket(listen_sock);
            break;
        }
        /* Check all possible sockets to see if there is pending data. */
        /* TODO: Efficiency: only check sockets that are in use. */
        for (i = 0; i <= nfds; i++) {
            if (!FD_ISSET(i, &readset)) {
                /* No pending data to be read from this socket. */
                continue;
            }
            if (i == listen_sock) {
                /* New incoming connection on listening socket. */
                addrsize = sizeof(client_address);
                memset(&client_address, 0, addrsize);
                newsock = accept(listen_sock,
                        (struct sockaddr *) &client_address, &addrsize);
                if (newsock == -1) {
                    KFS_ERROR("accept: %s", strerror(errno));
                    KFS_WARNING("Could not accept new connection.");
                } else {
                    ret = new_client(newsock);
                    if (ret == 0) {
                        KFS_INFO("Succesfully accepted connection.");
                        FD_SET(newsock, &allsocks);
                        nfds = max(newsock, nfds);
                    }
                }
            } else {
                /* Pending data on existing connection. */
                /* TODO: handle. */
                KFS_DEBUG("Data available from client.");
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
            ret = run_daemon(conf.port, kenny_oper);
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
