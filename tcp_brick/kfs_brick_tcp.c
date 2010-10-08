/**
 * KennyFS backend forwarding everything to a kennyfs server over TCP.
 *
 * TODO: Use the context argument to the operation handlers instead of global
 * variables to store state about the socket with the server (see connection.c).
 */

#define FUSE_USE_VERSION 29

#include "tcp_brick/kfs_brick_tcp.h"

#include <fuse.h>
#include <stdlib.h>

#include "minini/minini.h"

#include "kfs.h"
#include "kfs_api.h"
#include "kfs_misc.h"
#include "tcp_brick/connection.h"
#include "tcp_brick/handlers.h"
#include "tcp_brick/tcp_brick.h"

char hostname[256] = {'\0'};
char port[8] = {'\0'};

/**
 * Global initialization.
 */
static void *
kenny_init(const char *conffile, const char *section, size_t num_subvolumes,
        const struct kfs_brick subvolumes[])
{
    (void) subvolumes;

    const size_t hostname_size = NUMELEM(hostname);
    const size_t port_size = NUMELEM(port);
    struct conn_info conf = {.hostname = NULL, .port = NULL};
    int ret1 = 0;
    int ret2 = 0;

    KFS_ENTER();

    KFS_ASSERT(section != NULL && conffile != NULL && subvolumes != NULL);
    if (num_subvolumes != 0) {
        KFS_ERROR("Brick %s (TCP) takes no subvolumes.", section);
        KFS_RETURN(NULL);
    }
    ret1 = ini_gets(section, "hostname", "", hostname, hostname_size, conffile);
    ret2 = ini_gets(section, "port", "", port, port_size, conffile);
    if (ret1 == 0 || ret2 == 0) {
        KFS_ERROR("Did not find hostname and port for TCP brick in section `%s'"
                  " of configuration file %s.", section, conffile);
        KFS_RETURN(NULL);
    } else if (ret1 == hostname_size - 1) {
        KFS_ERROR("Value of hostname option in section `%s' of file %s too "
                  "long.", section, conffile);
        KFS_RETURN(NULL);
    } else if (ret2 == port_size - 1) {
        KFS_ERROR("Value of port option in section `%s' of file %s too long.",
                section, conffile);
        KFS_RETURN(NULL);
    }
    conf.hostname = hostname;
    conf.port = port;
    ret1 = init_connection(&conf);
    if (ret1 != 0) {
        KFS_RETURN(NULL);
    }
    ret1 = init_handlers();
    if (ret1 != 0) {
        KFS_RETURN(NULL);
    }

    /* Any non-NULL value indicates success, and context is not yet used. */
    KFS_RETURN(kenny_init);
}

/**
 * Global cleanup.
 */
static void
kenny_halt(void *private_data)
{
    KFS_NASSERT((void) private_data);

    KFS_ENTER();

    /* As returned by init() (only for the sake of this assert). */
    KFS_ASSERT(private_data == kenny_init);
    /** TODO: Close connection and handlers. */

    KFS_RETURN();
}

static const struct kfs_brick_api kenny_api = {
    .init = kenny_init,
    .getfuncs = get_handlers,
    .halt = kenny_halt,
};

const struct kfs_brick_api *
kfs_brick_getapi(void)
{
    KFS_ENTER();

    KFS_RETURN(&kenny_api);
}
