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
    KFS_DEBUG("Test: x.");
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
    uint32_t size_host = 0;
    uint32_t size_net = 0;
    uint16_t id = 0;
    int ret = 0;
    char *operbuf = NULL;

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

#if 0
static int
kenny_open(const char *fusepath, struct fuse_file_info *fi)
{
    KFS_ENTER();

    KFS_RETURN(-1);
}

/**
 * Read the contents of given file.
 * TODO: Check compliance of return value with API.
 */
static int
kenny_read(const char *fusepath, char *buf, size_t size, off_t offset, struct
        fuse_file_info *fi)
{
    KFS_ENTER();

    KFS_RETURN(-1);
}

/*
 * Extended attributes.
 */

static int
kenny_setxattr(const char *fusepath, const char *name, const char *value, size_t
        size, int flags)
{
    KFS_ENTER();

    KFS_RETURN(-1);
}

static int
kenny_getxattr(const char *fusepath, const char *name, char *value, size_t size)
{
    KFS_ENTER();

    KFS_RETURN(-1);
}

static int
kenny_listxattr(const char *fusepath, char *list, size_t size)
{
    KFS_ENTER();

    KFS_RETURN(-1);
}

static int
kenny_removexattr(const char *fusepath, const char *name)
{
    KFS_ENTER();

    KFS_RETURN(-1);
}

/*
 * Directories.
 */

static int
kenny_opendir(const char *fusepath, struct fuse_file_info *fi)
{
    KFS_ENTER();

    KFS_RETURN(-1);
}

/**
 * List directory contents.
 */
static int
kenny_readdir(const char *fusepath, void *buf, fuse_fill_dir_t filler,
        off_t offset, struct fuse_file_info *fi)
{
    KFS_ENTER();

    KFS_RETURN(-1);
}

static int
kenny_releasedir(const char *fusepath, struct fuse_file_info *fi)
{
    KFS_ENTER();

    KFS_RETURN(-1);
}

/**
 * Update access/modification time.
 */
static int
kenny_utimens(const char *fusepath, const struct timespec tvnano[2])
{
    KFS_ENTER();

    KFS_RETURN(-1);
}
#endif

static const struct fuse_operations handlers = {
    .getattr = kenny_getattr,
    .readlink = kenny_readlink,
#if 0
    .open = kenny_open,
    .read = kenny_read,
    .setxattr = kenny_setxattr,
    .getxattr = kenny_getxattr,
    .listxattr = kenny_listxattr,
    .removexattr = kenny_removexattr,
    .opendir = kenny_opendir,
    .readdir = kenny_readdir,
    .releasedir = kenny_releasedir,
    .utimens = kenny_utimens,
#endif
};

const struct fuse_operations *
get_handlers(void)
{
    KFS_ENTER();

    KFS_ASSERT(sizeof(uint16_t) == 2 && sizeof(uint32_t) == 4);

    KFS_RETURN(&handlers);
}
