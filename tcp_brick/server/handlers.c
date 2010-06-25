/**
 * Handlers for the kennyfs network server. Simply maps kennyfs network server
 * API to FUSE API by deserializing the operation arguments. Actual work is done
 * by the dynamically loaded backend brick(s).
 *
 * Separate file from rest of server code because that one was getting really
 * big.
 */

#include "tcp_brick/server/handlers.h"

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <fuse_opt.h>

#include "tcp_brick/tcp_brick.h"
#include "tcp_brick/server/server.h"

static const struct fuse_operations *oper = NULL;

/**
 * Handles a QUIT message.
 */
static int
handle_quit(client_t c, const char *rawop, size_t opsize)
{
    (void) c;
    (void) rawop;

    int ret = 0;

    KFS_ENTER();

    if (opsize > 0) {
        ret = -1;
    } else {
        ret = 2;
    }

    KFS_RETURN(ret);
}

/**
 * Lookup table for operation handlers.
 */
static const handler_t handlers[KFS_OPID_MAX_] = {
    [KFS_OPID_QUIT] = handle_quit
};

void
init_handlers(const struct fuse_operations *kenny_oper)
{
    KFS_ENTER();

    oper = kenny_oper;

    KFS_RETURN();
}

const handler_t *
get_handlers(void)
{
    KFS_ENTER();

    KFS_RETURN(handlers);
}
