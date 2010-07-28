/**
 * KennyFS backend forwarding everything to a kennyfs server over TCP.
 */

#define FUSE_USE_VERSION 29

#include "tcp_brick/kfs_brick_tcp.h"

#include <fuse.h>
#include <stdlib.h>

#include "kfs.h"
#include "kfs_api.h"
#include "kfs_misc.h"
#include "tcp_brick/connection.h"
#include "tcp_brick/handlers.h"
#include "tcp_brick/tcp_brick.h"

static struct kfs_brick_tcp_arg *myconf;

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
    KFS_NASSERT((void) len);

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
    ret = init_connection(myconf);
    if (ret == 0) {
        ret = init_handlers();
    }

    KFS_RETURN(ret);
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
    .getfuncs = get_handlers,
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
