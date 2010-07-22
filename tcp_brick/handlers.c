/**
 * Handlers for operations passed down to the kennyfs TCP brick. Documentation
 * for the format of operation messages and their return counterparts can be
 * found in the TCP brick server documentation.
 */

#define FUSE_USE_VERSION 29

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
 * do_operation) -EREMOTEIO is returned, otherwise any return value received
 * from the server is directly returned. This means that it is impossible to
 * distinguish between a local and a remote EREMOTEIO, except by having a look
 * at the logs.
 */
static int
do_operation_wrapper(enum fuse_op_id id, char *operbuf, size_t size,
                     char *resbuf, size_t resbuflen)
{
    int ret = 0;
    int serverret = 0;
    uint32_t size_serialised = 0;
    uint16_t id_serialised = 0;

    KFS_ENTER();

    size_serialised = htonl(size);
    id_serialised = htons(id);
    memcpy(operbuf, &size_serialised, 4);
    memcpy(operbuf + 4, &id_serialised, 2);
    ret = do_operation(operbuf, size + 6, resbuf, resbuflen, &serverret);
    if (ret == -1) {
        /* Client side failure. */
        KFS_RETURN(-EREMOTEIO);
    }
    if (serverret < 0) {
        KFS_INFO("Remote side responded with error %d: %s.", serverret,
                strerror(-serverret));
    }

    KFS_RETURN(serverret);
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
kenny_mknod(const char *path, mode_t mode, dev_t dev)
{
    char *operbuf = NULL;
    int ret = 0;
    size_t pathlen = 0;
    uint32_t mode_serialised;

    KFS_ENTER();

    if (dev != 0) {
        KFS_INFO("Calling mknod with non-zero dev arg is not supported "
                 "by the TCP brick.");
        KFS_RETURN(-ENOTSUP);
    }
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

static int
kenny_rmdir(const char *path)
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
    ret = do_operation_wrapper(KFS_OPID_RMDIR, operbuf, pathlen, NULL, 0);
    operbuf = KFS_FREE(operbuf);

    KFS_RETURN(ret);
}

static int
kenny_symlink(const char *path1, const char *path2)
{
    char *operbuf = NULL;
    size_t path1len = 0;
    size_t path2len = 0;
    size_t opersize = 0;
    uint32_t path1len_net = 0;
    int ret = 0;

    KFS_ENTER();

    path1len = strlen(path1);
    path2len = strlen(path2);
    opersize = 4 + path1len + 1 + path2len;
    operbuf = KFS_MALLOC(opersize + 6);
    if (operbuf == NULL) {
        KFS_RETURN(-ENOMEM);
    }
    path1len_net = htonl(path1len);
    memcpy(operbuf + 6, &path1len_net, 4);
    memcpy(operbuf + 10, path1, path1len);
    operbuf[10 + path1len] = '\0';
    memcpy(operbuf + 10 + path1len + 1, path2, path2len);
    ret = do_operation_wrapper(KFS_OPID_SYMLINK, operbuf, opersize, NULL, 0);
    operbuf = KFS_FREE(operbuf);

    KFS_RETURN(ret);
}

static int
kenny_rename(const char *path1, const char *path2)
{
    char *operbuf = NULL;
    size_t path1len = 0;
    size_t path2len = 0;
    size_t opersize = 0;
    uint32_t path1len_net = 0;
    int ret = 0;

    KFS_ENTER();

    path1len = strlen(path1);
    path2len = strlen(path2);
    opersize = 4 + path1len + 1 + path2len;
    operbuf = KFS_MALLOC(opersize + 6);
    if (operbuf == NULL) {
        KFS_RETURN(-ENOMEM);
    }
    path1len_net = htonl(path1len);
    memcpy(operbuf + 6, &path1len_net, 4);
    memcpy(operbuf + 10, path1, path1len);
    operbuf[10 + path1len] = '\0';
    memcpy(operbuf + 10 + path1len + 1, path2, path2len);
    ret = do_operation_wrapper(KFS_OPID_RENAME, operbuf, opersize, NULL, 0);
    operbuf = KFS_FREE(operbuf);

    KFS_RETURN(ret);
}

static int
kenny_link(const char *path1, const char *path2)
{
    char *operbuf = NULL;
    size_t path1len = 0;
    size_t path2len = 0;
    size_t opersize = 0;
    uint32_t path1len_net = 0;
    int ret = 0;

    KFS_ENTER();

    path1len = strlen(path1);
    path2len = strlen(path2);
    opersize = 4 + path1len + 1 + path2len;
    operbuf = KFS_MALLOC(opersize + 6);
    if (operbuf == NULL) {
        KFS_RETURN(-ENOMEM);
    }
    path1len_net = htonl(path1len);
    memcpy(operbuf + 6, &path1len_net, 4);
    memcpy(operbuf + 10, path1, path1len);
    operbuf[10 + path1len] = '\0';
    memcpy(operbuf + 10 + path1len + 1, path2, path2len);
    ret = do_operation_wrapper(KFS_OPID_LINK, operbuf, opersize, NULL, 0);
    operbuf = KFS_FREE(operbuf);

    KFS_RETURN(ret);
}

static int
kenny_chmod(const char *path, mode_t mode)
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
    ret = do_operation_wrapper(KFS_OPID_CHMOD, operbuf, pathlen + 4, NULL, 0);
    operbuf = KFS_FREE(operbuf);

    KFS_RETURN(ret);
}

static int
kenny_chown(const char *path, uid_t uid, gid_t gid)
{
    char *operbuf = NULL;
    int ret = 0;
    size_t pathlen = 0;
    uint32_t uid_serialised;
    uint32_t gid_serialised;

    KFS_ENTER();

    pathlen = strlen(path);
    uid_serialised = htonl(uid);
    gid_serialised = htonl(gid);
    operbuf = KFS_MALLOC(pathlen + 8 + 6);
    if (operbuf == NULL) {
        KFS_RETURN(-ENOMEM);
    }
    memcpy(operbuf + 6, &uid_serialised, 4);
    memcpy(operbuf + 10, &gid_serialised, 4);
    memcpy(operbuf + 14, path, pathlen);
    ret = do_operation_wrapper(KFS_OPID_CHOWN, operbuf, pathlen + 8, NULL, 0);
    operbuf = KFS_FREE(operbuf);

    KFS_RETURN(ret);
}

static int
kenny_truncate(const char *path, off_t offset)
{
    char *operbuf = NULL;
    int ret = 0;
    size_t pathlen = 0;
    uint64_t offset_serialised;

    KFS_ENTER();

    pathlen = strlen(path);
    offset_serialised = htonll(offset);
    operbuf = KFS_MALLOC(pathlen + 8 + 6);
    if (operbuf == NULL) {
        KFS_RETURN(-ENOMEM);
    }
    memcpy(operbuf + 6, &offset_serialised, 8);
    memcpy(operbuf + 14, path, pathlen);
    ret = do_operation_wrapper(KFS_OPID_TRUNCATE, operbuf, pathlen + 8, NULL,
            0);
    operbuf = KFS_FREE(operbuf);

    KFS_RETURN(ret);
}

static int
kenny_open(const char *path, struct fuse_file_info *ffi)
{
    char resbuf[9];
    char *operbuf = NULL;
    int ret = 0;
    uint32_t flags_serialised = 0;
    size_t pathlen = 0;

    KFS_ENTER();

    pathlen = strlen(path);
    operbuf = KFS_MALLOC(pathlen + 5 + 6);
    if (operbuf == NULL) {
        KFS_RETURN(-ENOMEM);
    }
    flags_serialised = htonl(ffi->flags);
    memcpy(operbuf + 6, &flags_serialised, 4);
    memcpy(operbuf + 10, path, pathlen);
    ret = do_operation_wrapper(KFS_OPID_OPEN, operbuf, pathlen + 5, resbuf,
            sizeof(resbuf));
    operbuf = KFS_FREE(operbuf);
    if (ret == 0) {
        KFS_ASSERT(sizeof(ffi->fh) == 8);
        memcpy(&(ffi->fh), resbuf, 8);
        ffi->direct_io = (resbuf[9] << 0) & 1;
        ffi->keep_cache = (resbuf[9] << 1) & 1;
#if FUSE_VERSION >= 29
        ffi->nonseekable = (resbuf[9] << 2) & 1;
#endif
    }

    KFS_RETURN(ret);
}

static int
kenny_read(const char *path, char *buf, size_t nbyte, off_t offset, struct
        fuse_file_info *ffi)
{
    (void) path;

    char operbuf[20 + 6];
    uint64_t val64 = 0;
    uint32_t val32 = 0;
    int ret = 0;

    KFS_ENTER();

    /* The file handle. */
    memcpy(operbuf + 6, &ffi->fh, 8);
    /* The number of bytes to read. */
    val32 = htonl(nbyte);
    memcpy(operbuf + 14, &val32, 4);
    /* The offset in the file. */
    val64 = htonll(offset);
    memcpy(operbuf + 18, &val64, 8);
    ret = do_operation_wrapper(KFS_OPID_READ, operbuf, 20, buf, nbyte);

    KFS_RETURN(ret);
}

static const struct fuse_operations handlers = {
    .getattr = kenny_getattr,
    .readlink = kenny_readlink,
    .mknod = kenny_mknod,
    .mkdir = kenny_mkdir,
    .unlink = kenny_unlink,
    .rmdir = kenny_rmdir,
    .symlink = kenny_symlink,
    .rename = kenny_rename,
    .link = kenny_link,
    .chmod = kenny_chmod,
    .chown = kenny_chown,
    .truncate = kenny_truncate,
    .open = kenny_open,
    .read = kenny_read,
};

const struct fuse_operations *
get_handlers(void)
{
    KFS_ENTER();

    KFS_ASSERT(sizeof(uint16_t) == 2 && sizeof(uint32_t) == 4);

    KFS_RETURN(&handlers);
}
