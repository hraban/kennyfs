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

static int
kenny_getattr(const char *fusepath, struct stat *stbuf)
{
    uint32_t intbuf[13];
    char resbuf[sizeof(intbuf)];
    char *operbuf = NULL;
    uint32_t size_host = 0;
    uint32_t size_net = 0;
    uint16_t id = 0;
    int ret = 0;

    KFS_ENTER();

    size_host = strlen(fusepath);
    size_net = htonl(size_host);
    id = htons(KFS_OPID_GETATTR);
    operbuf = KFS_MALLOC(size_host + 6);
    if (operbuf == NULL) {
        KFS_RETURN(-ENOMEM);
    }
    memcpy(operbuf, &size_net, 4);
    memcpy(operbuf + 4, &id, 2);
    memcpy(operbuf + 6, fusepath, size_host);
    ret = do_operation(operbuf, size_host + 6, resbuf, sizeof(resbuf));
    KFS_ASSERT(ret >= -1);
    operbuf = KFS_FREE(operbuf);
    if (ret == -1) {
        /* Something went wrong on our side, call it "remote I/O error." */
        KFS_RETURN(-EREMOTEIO);
    } else if (ret != 0) {
        /* Something went wrong on the other side, pass on the error. */
        KFS_ERROR("Remote side responded with error %d: %s.", ret,
                strerror(ret));
        KFS_RETURN(-ret);
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
kenny_readlink(const char *fusepath, char *buf, size_t size)
{
    char *operbuf = NULL;
    int ret = 0;
    uint32_t size_host = 0;
    uint32_t size_net = 0;
    uint16_t id = 0;

    KFS_ENTER();

    size_host = strlen(fusepath);
    size_net = htonl(size_host);
    id = htons(KFS_OPID_READLINK);
    operbuf = KFS_MALLOC(size_host + 6);
    if (operbuf == NULL) {
        KFS_RETURN(-ENOMEM);
    }
    memcpy(operbuf, &size_net, 4);
    memcpy(operbuf + 4, &id, 2);
    memcpy(operbuf + 6, fusepath, size_host);
    ret = do_operation(operbuf, size_host + 6, buf, size);
    KFS_ASSERT(ret >= -1);
    operbuf = KFS_FREE(operbuf);
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
kenny_mkdir(const char *path, mode_t mode)
{
    char *operbuf = NULL;
    int ret = 0;
    uint32_t size_host = 0;
    uint32_t size_net = 0;
    uint32_t mode_serialised;
    uint16_t id = 0;

    KFS_ENTER();

    size_host = strlen(path) + 4;
    size_net = htonl(size_host);
    id = htons(KFS_OPID_MKDIR);
    mode_serialised = htonl(mode);
    operbuf = KFS_MALLOC(size_host + 6);
    if (operbuf == NULL) {
        KFS_RETURN(-ENOMEM);
    }
    memcpy(operbuf, &size_net, 4);
    memcpy(operbuf + 4, &id, 2);
    memcpy(operbuf + 6, &mode_serialised, 4);
    memcpy(operbuf + 10, path, size_host - 4);
    ret = do_operation(operbuf, size_host + 6, NULL, 0);
    KFS_ASSERT(ret >= -1);
    operbuf = KFS_FREE(operbuf);
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
kenny_unlink(const char *path)
{
    uint32_t size_host = 0;
    uint32_t size_net = 0;
    uint16_t id = 0;
    int ret = 0;
    char *operbuf = NULL;

    KFS_ENTER();

    size_host = strlen(path);
    size_net = htonl(size_host);
    id = htons(KFS_OPID_UNLINK);
    operbuf = KFS_MALLOC(size_host + 6);
    if (operbuf == NULL) {
        KFS_RETURN(-ENOMEM);
    }
    memcpy(operbuf, &size_net, 4);
    memcpy(operbuf + 4, &id, 2);
    memcpy(operbuf + 6, path, size_host);
    ret = do_operation(operbuf, size_host + 6, NULL, 0);
    operbuf = KFS_FREE(operbuf);
    if (ret == -1) {
        KFS_RETURN(-EREMOTEIO);
    } else if (ret != 0) {
        KFS_ERROR("Remote side responded with error %d: %s.", ret,
                strerror(ret));
        KFS_RETURN(-ret);
    }

    KFS_RETURN(0);
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
