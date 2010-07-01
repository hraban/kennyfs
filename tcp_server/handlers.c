/**
 * Handlers for the kennyfs network server. Simply maps kennyfs network server
 * API to FUSE API by deserializing the operation arguments. Actual work is done
 * by the dynamically loaded backend brick(s).
 *
 * Separate file from rest of server code because that one was getting really
 * big.
 */

#define FUSE_USE_VERSION 26

#include "tcp_server/handlers.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fuse.h>
#include <fuse_opt.h>
#include <string.h>
#include <sys/stat.h>

#include "kfs_misc.h"
#include "kfs_memory.h"
#include "tcp_brick/tcp_brick.h"
#include "tcp_server/server.h"

static const struct fuse_operations *oper = NULL;

static int
handle_getattr(client_t c, const char *rawop, size_t opsize)
{
    uint32_t intbuf[13];
    char resbuf[4 + sizeof(intbuf)];
    char *fname = NULL;
    uint32_t retval = 0;
    int ret = 0;
    struct stat stbuf;

    KFS_ENTER();

    fname = KFS_MALLOC(opsize + 1);
    if (fname == NULL) {
        ret = -ENOMEM;
    } else {
        fname = memcpy(fname, rawop, opsize);
        fname[opsize] = '\0';
        ret = oper->getattr(fname, &stbuf);
    }
    KFS_ASSERT(ret <= 0);
    /* Send the absolute value over the wire. */
    retval = htonl(-ret);
    memcpy(resbuf, &retval, 4);
    if (ret == 0) {
        /* Call succeeded, also send the body. */
        /* TODO: Check if this cast is legal, portable and complete. */
        intbuf[0] = htonl(stbuf.st_dev);
        intbuf[1] = htonl(stbuf.st_ino);
        intbuf[2] = htonl(stbuf.st_mode);
        intbuf[3] = htonl(stbuf.st_nlink);
        intbuf[4] = htonl(stbuf.st_uid);
        intbuf[5] = htonl(stbuf.st_gid);
        intbuf[6] = htonl(stbuf.st_rdev);
        intbuf[7] = htonl(stbuf.st_size);
        intbuf[8] = htonl(stbuf.st_blksize);
        intbuf[9] = htonl(stbuf.st_blocks);
        intbuf[10] = htonl(stbuf.st_atime);
        intbuf[11] = htonl(stbuf.st_mtime);
        intbuf[12] = htonl(stbuf.st_ctime);
        memcpy(resbuf + 4, intbuf, sizeof(intbuf));
        ret = send_msg(c, resbuf, sizeof(resbuf));
    } else {
        /* Call failed, return only the error code. */
        ret = send_msg(c, resbuf, 4);
    }
    if (fname != NULL) {
        fname = KFS_FREE(fname);
    }

    KFS_RETURN(ret);
}

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
    [KFS_OPID_GETATTR] = handle_getattr,
    [KFS_OPID_QUIT] = handle_quit,
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
