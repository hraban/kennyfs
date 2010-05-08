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
    int port;
};

/**
 * Create a socket that listens for incoming TCP connections on given port.
 */
static int
create_listen_socket(int port)
{
    const int yes = 1;
    struct sockaddr_in a;
    const struct protoent *pe = NULL;
    int listen_sock = 0;
    int protonum = 0;
    int ret = 0;

    /* Create the socket, set its type and options. */
    pe = getprotobyname("tcp");
    if (pe == NULL) {
        protonum = 0;
    } else {
        protonum = pe->p_proto;
    }
    listen_sock = socket(AF_INET, SOCK_STREAM, protonum);
    if (listen_sock == -1) {
        KFS_ERROR("socket: %s", strerror(errno));
        KFS_RETURN(-1);
    }
    ret = setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (ret == 0) {
        /* Bind it to a local address and port. */
        memset(&a, 0, sizeof(a));
        a.sin_port = htons(port);
        a.sin_family = AF_INET;
        ret = bind(listen_sock, (struct sockaddr *) &a, sizeof(a));
        if (ret == 0) {
            /* Indicate willigness to accept incoming connections. */
            ret = listen(listen_sock, 10);
            if (ret == -1) {
                KFS_ERROR("listen: %s", strerror(errno));
            }
        } else {
            KFS_ERROR("bind: %s", strerror(errno));
        }
    } else {
        KFS_ERROR("setsockopt: %s", strerror(errno));
    }
    if (ret == -1) {
        ret = close(listen_sock);
        if (ret == -1) {
            KFS_ERROR("close: %s", strerror(errno));
        }
        ret = -1;
    } else {
        ret = listen_sock;
    }

    KFS_RETURN(ret);
}

/**
 * Listen for incoming connections and handle them.
 */
static int
start_daemon(int port, const struct fuse_operations *kenny_oper)
{
    (void) kenny_oper;
    fd_set readset;
    int ret = 0;
    /* Socket listening for incoming connections. */
    int listen_sock = 0;
    int nfds = 0;

    KFS_ENTER();

    ret = 0;
    FD_ZERO(&readset);
    listen_sock = create_listen_socket(port);
    if (listen_sock == -1) {
        KFS_RETURN(-1);
    }
    FD_SET(listen_sock, &readset);
    nfds = listen_sock + 1;
    ret = select(nfds, &readset, NULL, NULL, NULL);
    KFS_ERROR("TEST2");
    if (ret == -1) {
        KFS_ERROR("select: %s", strerror(errno));
        ret = close(listen_sock);
        if (ret == -1) {
            KFS_ERROR("close: %s", strerror(errno));
        }
        KFS_RETURN(-1);
    }
    if (FD_ISSET(listen_sock, &readset)) {
        /* Handle new incoming connection. */
        size_t addrsize = 0;
        struct sockaddr_in client_address;

        addrsize = sizeof(client_address);
        memset(&client_address, 0, addrsize);
        ret = accept(listen_sock, (struct sockaddr *) &client_address,
                &addrsize);
        if (ret == -1) {
            KFS_ERROR("accept: %s", strerror(errno));
        } else {
            KFS_DEBUG("Succesfully accepted connection.");
        }
        ret = close(listen_sock);
        if (ret == -1) {
            KFS_ERROR("close: %s", strerror(errno));
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

    return ret;
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
        conf.port = atoi(argv[3]);
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
            ret = start_daemon(conf.port, kenny_oper);
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
