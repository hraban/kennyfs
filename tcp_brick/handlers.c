/**
 * Handlers for operations passed down to the kennyfs TCP brick.
 */

#define FUSE_USE_VERSION 26

#include "tcp_brick/handlers.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fuse.h>
#include <fuse_opt.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

#include "kfs.h"
#include "kfs_misc.h"
#include "tcp_brick/connection.h"
#include "tcp_brick/tcp_brick.h"

/**
 * Wrapper around the do_operation() routine from tcp_brick/connection.c.
 * Expects a buffer filled with `size' bytes, but starting at element 6 (not 0),
 * meaning that the buffer is in fact 6 bytes bigger than `size'.  This wrapper
 * will fill in the first six bytes as per the protocol. The return buffers are
 * handled just as by do_operation(). On failure by the client (i.e.: by
 * do_operation) -EREMOTEIO is returned, otherwise the negated value of whatever
 * return value is sent back by the server is returned.
 */
static int
do_operation_wrapper(enum fuse_op_id id, char *operbuf, size_t size,
                     char *resbuf, size_t resbuflen)
{
    int ret = 0;
    uint32_t size_serialised = 0;
    uint16_t id_serialised = 0;

    KFS_ENTER();

    size_serialised = htonl(size);
    id_serialised = htons(id);
    memcpy(operbuf, &size_serialised, 4);
    memcpy(operbuf + 4, &id_serialised, 2);
    ret = do_operation(operbuf, size + 6, resbuf, resbuflen);
    KFS_ASSERT(ret >= -1);
    if (ret == -1) {
        KFS_RETURN(-EREMOTEIO);
    } else if (ret != 0) {
        KFS_ERROR("Remote side responded with error %d: %s.", ret,
                strerror(ret));
        KFS_RETURN(-ret);
    }

    KFS_RETURN(0);
}

static int
kenny_getattr(const char *fusepath, struct stat *stbuf)
{
    uint32_t intbuf[13];
    char resbuf[sizeof(intbuf)];
    char *operbuf = NULL;
    size_t pathlen = 0;
    int ret = 0;

    KFS_ENTER();

    pathlen = strlen(fusepath);
    operbuf = KFS_MALLOC(pathlen + 6);
    if (operbuf == NULL) {
        KFS_RETURN(-ENOMEM);
    }
    memcpy(operbuf + 6, fusepath, pathlen);
    ret = do_operation_wrapper(KFS_OPID_GETATTR, operbuf, pathlen, resbuf,
            sizeof(resbuf));
    operbuf = KFS_FREE(operbuf);
    if (ret != 0) {
        KFS_RETURN(ret);
    }
    KFS_ASSERT(sizeof(intbuf) == sizeof(resbuf));
    memcpy(intbuf, resbuf, sizeof(intbuf));
    stbuf->st_dev = ntohl(intbuf[0]);
    stbuf->st_ino = ntohl(intbuf[1]);
    stbuf->st_mode = ntohl(intbuf[2]);
    stbuf->st_nlink = ntohl(intbuf[3]);
    stbuf->st_uid = ntohl(intbuf[4]);
    stbuf->st_gid = ntohl(intbuf[5]);
    stbuf->st_rdev = ntohl(intbuf[6]);
    stbuf->st_size = ntohl(intbuf[7]);
    stbuf->st_blksize = ntohl(intbuf[8]);
    stbuf->st_blocks = ntohl(intbuf[9]);
    stbuf->st_atime = ntohl(intbuf[10]);
    stbuf->st_mtime = ntohl(intbuf[11]);
    stbuf->st_ctime = ntohl(intbuf[12]);

    KFS_RETURN(0);
}

/**
 * Read the target of a symlink.
 */
static int
kenny_readlink(const char *path, char *buf, size_t size)
{
    char *operbuf = NULL;
    int ret = 0;
    size_t pathlen = 0;

    KFS_ENTER();

    pathlen = strlen(path);
    operbuf = KFS_MALLOC(pathlen + 6);
    if (operbuf == NULL) {
        KFS_RETURN(-ENOMEM);
    }
    memcpy(operbuf + 6, path, pathlen);
    ret = do_operation_wrapper(KFS_OPID_READLINK, operbuf, pathlen, buf, size);
    operbuf = KFS_FREE(operbuf);

    KFS_RETURN(ret);
}

static int
kenny_mkdir(const char *path, mode_t mode)
{
    char *operbuf = NULL;
    int ret = 0;
    size_t pathlen = 0;
    uint32_t mode_serialised;

    KFS_ENTER();

    pathlen = strlen(path);
    mode_serialised = htonl(mode);
    operbuf = KFS_MALLOC(pathlen + 4 + 6);
    if (operbuf == NULL) {
        KFS_RETURN(-ENOMEM);
    }
    memcpy(operbuf + 6, &mode_serialised, 4);
    memcpy(operbuf + 10, path, pathlen);
    ret = do_operation_wrapper(KFS_OPID_MKDIR, operbuf, pathlen + 4, NULL, 0);
    operbuf = KFS_FREE(operbuf);

    KFS_RETURN(ret);
}

static int
kenny_unlink(const char *path)
{
    char *operbuf = NULL;
    size_t pathlen = 0;
    int ret = 0;

    KFS_ENTER();

    pathlen = strlen(path);
    operbuf = KFS_MALLOC(pathlen + 6);
    if (operbuf == NULL) {
        KFS_RETURN(-ENOMEM);
    }
    memcpy(operbuf + 6, path, pathlen);
    ret = do_operation_wrapper(KFS_OPID_UNLINK, operbuf, pathlen, NULL, 0);
    operbuf = KFS_FREE(operbuf);

    KFS_RETURN(ret);
}

static const struct fuse_operations handlers = {
    .getattr = kenny_getattr,
    .readlink = kenny_readlink,
    .mkdir = kenny_mkdir,
    .unlink = kenny_unlink,
};

const struct fuse_operations *
get_handlers(void)
{
    KFS_ENTER();

    KFS_ASSERT(sizeof(uint16_t) == 2 && sizeof(uint32_t) == 4);

    KFS_RETURN(&handlers);
}
