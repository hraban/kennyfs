/**
 * Handlers for the kennyfs network server. Simply maps kennyfs network server
 * API to FUSE API by deserializing the operation arguments. Actual work is done
 * by the dynamically loaded backend brick(s).
 *
 * Comments of operation handlers in this file describe the format of the
 * operation and the return message. These comments are about not about the
 * headers (the leading bytes with fixed meaning, see tcp_brick.h) but about the
 * variable sized body parts with operation-defined data.
 */

#define FUSE_USE_VERSION 29

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

/**
 * Handle a getattr operation. The argument message is the raw pathname. The
 * return message is a struct stat serialised as 13 htonl()'ed uint32_t's. The
 * elements are ordered as follows:
 *
 * - st_dev
 * - st_ino
 * - st_mode
 * - st_nlink
 * - st_uid
 * - st_gid
 * - st_rdev
 * - st_size
 * - st_blksize
 * - st_blocks
 * - st_atime
 * - st_mtime
 * - st_ctime
 *
 * This assumes that all those values are of a type that entirely fits in a
 * uint32_t. TODO: Check if that is always the case.
 */
static int
handle_getattr(client_t c, const char *rawop, size_t opsize)
{
    (void) opsize;

    uint32_t intbuf[13];
    char resbuf[8 + sizeof(intbuf)];
    uint32_t val = 0;
    int ret = 0;
    struct stat stbuf;

    KFS_ENTER();

    if (oper->getattr == NULL) {
        /* Not implemented in backend. */
        ret = -ENOSYS;
    } else {
        ret = oper->getattr(rawop, &stbuf);
    }
    KFS_ASSERT(ret <= 0);
    /* Send the absolute value over the wire. */
    val = htonl(-ret);
    memcpy(resbuf, &val, 4);
    if (ret == 0) {
        /* Call succeeded, also send the body. */
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
        val = htonl(sizeof(intbuf));
        memcpy(resbuf + 4, &val, 4);
        memcpy(resbuf + 8, intbuf, sizeof(intbuf));
        ret = send_msg(c, resbuf, sizeof(resbuf));
    } else {
        /* Call failed, return only the error code. */
        val = htonl(0);
        memcpy(resbuf + 4, &val, 4);
        ret = send_msg(c, resbuf, 8);
    }

    KFS_RETURN(ret);
}

/**
 * Handle a readlink operation. The argument message is the raw pathname. The
 * return message is the raw contents of the link.
 */
static int
handle_readlink(client_t c, const char *rawop, size_t opsize)
{
    (void) opsize;

    uint32_t val = 0;
    int ret = 0;
    size_t len = 0;
    char resultbuf[PATHBUF_SIZE + 8];

    KFS_ENTER();

    if (oper->readlink == NULL) {
        ret = -ENOSYS;
    } else {
        ret = oper->readlink(rawop, resultbuf + 8, sizeof(resultbuf) - 8);
    }
    KFS_ASSERT(ret <= 0);
    val = htonl(-ret);
    memcpy(resultbuf, &val, 4);
    if (ret == 0) {
        len = strlen(resultbuf + 8);
        val = htonl(len);
        memcpy(resultbuf + 4, &val, 4);
        ret = send_msg(c, resultbuf, len + 8);
    } else {
        val = htonl(0);
        memcpy(resultbuf + 4, &val, 4);
        ret = send_msg(c, resultbuf, 8);
    }

    KFS_RETURN(ret);
}

/**
 * Handle a mknod operation. The argument message is a mode_t cast to a uint32_t
 * passed through htonl() (4 bytes) followed by the pathname. The return message
 * is empty. There is no `dev' argument: it is always 0. Anything else is not
 * supported by this protocol.
 */
static int
handle_mknod(client_t c, const char *rawop, size_t opsize)
{
    (void) opsize;

    char resultbuf[8];
    uint32_t mode_serialised = 0;
    uint32_t val = 0;
    mode_t mode;
    int ret = 0;

    KFS_ENTER();

    memcpy(&mode_serialised, rawop, 4);
    KFS_ASSERT(sizeof(uint32_t) >= sizeof(mode_t));
    mode = ntohl(mode_serialised); 
    if (oper->mknod == NULL) {
        ret = -ENOSYS;
    } else {
        ret = oper->mknod(rawop + 4, mode, 0);
    }
    KFS_ASSERT(ret <= 0);
    val = htonl(-ret);
    memcpy(resultbuf, &val, 4);
    val = htonl(0);
    memcpy(resultbuf + 4, &val, 4);
    ret = send_msg(c, resultbuf, 8);

    KFS_RETURN(ret);
}

/**
 * Handle a mkdir operation. The argument message is a mode_t cast to a uint32_t
 * passed through htonl() (4 bytes) followed by the pathname. The return message
 * is empty.
 */
static int
handle_mkdir(client_t c, const char *rawop, size_t opsize)
{
    (void) opsize;

    char resultbuf[8];
    uint32_t mode_serialised = 0;
    uint32_t val = 0;
    mode_t mode;
    int ret = 0;

    KFS_ENTER();

    memcpy(&mode_serialised, rawop, 4);
    KFS_ASSERT(sizeof(uint32_t) >= sizeof(mode_t));
    mode = ntohl(mode_serialised); 
    if (oper->mkdir == NULL) {
        ret = -ENOSYS;
    } else {
        ret = oper->mkdir(rawop + 4, mode);
    }
    KFS_ASSERT(ret <= 0);
    val = htonl(-ret);
    memcpy(resultbuf, &val, 4);
    val = htonl(0);
    memcpy(resultbuf + 4, &val, 4);
    ret = send_msg(c, resultbuf, 8);

    KFS_RETURN(ret);
}

/**
 * Handle an unlink operation. The argument message is the pathname. The return
 * message is empty.
 */
static int
handle_unlink(client_t c, const char *rawop, size_t opsize)
{
    (void) opsize;

    char resultbuf[8];
    uint32_t val = 0;
    int ret = 0;

    KFS_ENTER();

    if (oper->unlink == NULL) {
        ret = -ENOSYS;
    } else {
        ret = oper->unlink(rawop);
    }
    KFS_ASSERT(ret <= 0);
    val = htonl(-ret);
    memcpy(resultbuf, &val, 4);
    val = htonl(0);
    memcpy(resultbuf + 4, &val, 4);
    ret = send_msg(c, resultbuf, 8);

    KFS_RETURN(ret);
}

/**
 * Handle a rmdir operation. The argument message is the pathname. The return
 * message is empty.
 */
static int
handle_rmdir(client_t c, const char *rawop, size_t opsize)
{
    (void) opsize;

    char resultbuf[8];
    uint32_t val = 0;
    int ret = 0;

    KFS_ENTER();

    if (oper->rmdir == NULL) {
        ret = -ENOSYS;
    } else {
        ret = oper->rmdir(rawop);
    }
    KFS_ASSERT(ret <= 0);
    val = htonl(-ret);
    memcpy(resultbuf, &val, 4);
    val = htonl(0);
    memcpy(resultbuf + 4, &val, 4);
    ret = send_msg(c, resultbuf, 8);

    KFS_RETURN(ret);
}

/**
 * Handle a symlink operation. The argument message is the length of
 * path1 cast to a uint32_t passed through htonl() (4 bytes) followed by path1,
 * followed by one byte with value 0, followed by path2. The return message is
 * empty.
 */
static int
handle_symlink(client_t c, const char *rawop, size_t opsize)
{
    (void) opsize;

    char resultbuf[8];
    uint32_t val = 0;
    uint32_t path1len = 0;
    const char *path1 = NULL;
    const char *path2 = NULL;
    int ret2client = 0;
    int ret2server = 0;
    int temp = 0;

    KFS_ENTER();

    ret2server = 0;
    if (oper->symlink == NULL) {
        ret2client = -ENOSYS;
    } else {
        memcpy(&path1len, rawop, 4);
        path1len = ntohl(path1len);
        /* Check that the paths are separated by a '\0'-byte. */
        if (rawop[4 + path1len] != '\0') {
            ret2client = -EINVAL;
            /* Corrupt argument. */
            ret2server = -1;
        } else {
            path1 = rawop + 4;
            path2 = rawop + 4 + path1len + 1;
            ret2client = oper->symlink(path1, path2);
        }
    }
    KFS_ASSERT(ret2client <= 0);
    val = htonl(-ret2client);
    memcpy(resultbuf, &val, 4);
    val = htonl(0);
    memcpy(resultbuf + 4, &val, 4);
    temp = send_msg(c, resultbuf, 8);
    if (ret2server == 0) {
        ret2server = temp;
    }

    KFS_RETURN(ret2server);
}

/**
 * Handle a rename operation. The argument message is the length of path1 cast
 * to a uint32_t passed through htonl() (4 bytes) followed by path1, followed by
 * one byte with value 0, followed by path2. The return message is empty.
 */
static int
handle_rename(client_t c, const char *rawop, size_t opsize)
{
    (void) opsize;

    char resultbuf[8];
    uint32_t val = 0;
    uint32_t path1len = 0;
    const char *path1 = NULL;
    const char *path2 = NULL;
    int ret2client = 0;
    int ret2server = 0;
    int temp = 0;

    KFS_ENTER();

    ret2server = 0;
    if (oper->rename == NULL) {
        ret2client = -ENOSYS;
    } else {
        memcpy(&path1len, rawop, 4);
        path1len = ntohl(path1len);
        /* Check that the paths are separated by a '\0'-byte. */
        if (rawop[4 + path1len] != '\0') {
            ret2client = -EINVAL;
            /* Corrupt argument. */
            ret2server = -1;
        } else {
            path1 = rawop + 4;
            path2 = rawop + 4 + path1len + 1;
            ret2client = oper->rename(path1, path2);
        }
    }
    KFS_ASSERT(ret2client <= 0);
    val = htonl(-ret2client);
    memcpy(resultbuf, &val, 4);
    val = htonl(0);
    memcpy(resultbuf + 4, &val, 4);
    temp = send_msg(c, resultbuf, 8);
    if (ret2server == 0) {
        ret2server = temp;
    }

    KFS_RETURN(ret2server);
}

/**
 * Handle a link operation. The argument message is the length of path1 cast to
 * a uint32_t passed through htonl() (4 bytes) followed by path1, followed by
 * one byte with value 0, followed by path2. The return message is empty.
 */
static int
handle_link(client_t c, const char *rawop, size_t opsize)
{
    (void) opsize;

    char resultbuf[8];
    uint32_t val = 0;
    uint32_t path1len = 0;
    const char *path1 = NULL;
    const char *path2 = NULL;
    int ret2client = 0;
    int ret2server = 0;
    int temp = 0;

    KFS_ENTER();

    ret2server = 0;
    if (oper->link == NULL) {
        ret2client = -ENOSYS;
    } else {
        memcpy(&path1len, rawop, 4);
        path1len = ntohl(path1len);
        /* Check that the paths are separated by a '\0'-byte. */
        if (rawop[4 + path1len] != '\0') {
            ret2client = -EINVAL;
            /* Corrupt argument. */
            ret2server = -1;
        } else {
            path1 = rawop + 4;
            path2 = rawop + 4 + path1len + 1;
            ret2client = oper->link(path1, path2);
        }
    }
    KFS_ASSERT(ret2client <= 0);
    val = htonl(-ret2client);
    memcpy(resultbuf, &val, 4);
    val = htonl(0);
    memcpy(resultbuf + 4, &val, 4);
    temp = send_msg(c, resultbuf, 8);
    if (ret2server == 0) {
        ret2server = temp;
    }

    KFS_RETURN(ret2server);
}

/**
 * Handle a chmod operation. The argument message is a mode_t cast to a uint32_t
 * passed through htonl() (4 bytes) followed by the pathname. The return message
 * is empty.
 */
static int
handle_chmod(client_t c, const char *rawop, size_t opsize)
{
    (void) opsize;

    char resultbuf[8];
    uint32_t mode_serialised = 0;
    uint32_t val = 0;
    mode_t mode;
    int ret = 0;

    KFS_ENTER();

    memcpy(&mode_serialised, rawop, 4);
    KFS_ASSERT(sizeof(uint32_t) >= sizeof(mode_t));
    mode = ntohl(mode_serialised); 
    if (oper->chmod == NULL) {
        ret = -ENOSYS;
    } else {
        ret = oper->chmod(rawop + 4, mode);
    }
    KFS_ASSERT(ret <= 0);
    val = htonl(-ret);
    memcpy(resultbuf, &val, 4);
    val = htonl(0);
    memcpy(resultbuf + 4, &val, 4);
    ret = send_msg(c, resultbuf, 8);

    KFS_RETURN(ret);
}

/**
 * Handle a chown operation. The argument message is a uid_t cast to a uint32_t
 * passed through htonl() (4 bytes), followed by a gid_t (same story, 4 bytes),
 * followed by the pathname. The return message is empty. TODO: Check if those
 * casts to uint32_t are portable.
 */
static int
handle_chown(client_t c, const char *rawop, size_t opsize)
{
    (void) opsize;

    char resultbuf[8];
    uint32_t uid_serialised = 0;
    uint32_t gid_serialised = 0;
    uint32_t val = 0;
    uid_t uid = 0;
    gid_t gid = 0;
    int ret = 0;

    KFS_ENTER();

    memcpy(&uid_serialised, rawop, 4);
    memcpy(&gid_serialised, rawop, 4);
    KFS_ASSERT(sizeof(uint32_t) >= sizeof(uid_t));
    KFS_ASSERT(sizeof(uint32_t) >= sizeof(gid_t));
    uid = ntohl(uid_serialised);
    gid = ntohl(gid_serialised);
    if (oper->chown == NULL) {
        ret = -ENOSYS;
    } else {
        ret = oper->chown(rawop + 8, uid, gid);
    }
    KFS_ASSERT(ret <= 0);
    val = htonl(-ret);
    memcpy(resultbuf, &val, 4);
    val = htonl(0);
    memcpy(resultbuf + 4, &val, 4);
    ret = send_msg(c, resultbuf, 8);

    KFS_RETURN(ret);
}

/**
 * Handle a truncate operation. The argument message is a off_t cast to a
 * uint64_t passed through htonll() (8 bytes) followed by the pathname. The
 * return message is empty.
 */
static int
handle_truncate(client_t c, const char *rawop, size_t opsize)
{
    (void) opsize;

    char resultbuf[8];
    uint64_t offset_serialised = 0;
    uint32_t val = 0;
    off_t offset = 0;
    int ret = 0;

    KFS_ENTER();

    memcpy(&offset_serialised, rawop, 8);
    KFS_ASSERT(sizeof(uint64_t) >= sizeof(off_t));
    offset = ntohll(offset_serialised);
    if (oper->truncate == NULL) {
        ret = -ENOSYS;
    } else {
        ret = oper->truncate(rawop + 8, offset);
    }
    KFS_ASSERT(ret <= 0);
    val = htonl(-ret);
    memcpy(resultbuf, &val, 4);
    val = htonl(0);
    memcpy(resultbuf + 4, &val, 4);
    ret = send_msg(c, resultbuf, 8);

    KFS_RETURN(ret);
}

/**
 * Handle an open operation. The argument message consists of a serialized
 * fuse_file_info struct followed by the pathname. The struct is serialized as
 * follows:
 *
 * - 4 bytes: open flags, cast to uint32_t, in network order.
 * - 1 byte: fuse flags, a bitmask of the following flags (from lsb to msb):
 *   direct_io, keep_cache, nonseekable.
 *
 * The return message is an 8-byte character array that will be used to uniquely
 * identify this file in further operations (a file handler). It is, in fact,
 * the fh element, but that is an implementation detail.
 *
 * If the FUSE library installed on the server has a version lower than 2.9 the
 * nonseekable flag is not supported: an operation with that flag set will
 * result in a ENOTSUP error being sent back to the client.
 *
 * TODO: The flags element is an int in the original struct, which is larger
 * than a uint32_t on some architectures. Can that become a problem?
 */
static int
handle_open(client_t c, const char *rawop, size_t opsize)
{
    (void) opsize;

    struct fuse_file_info ffi;
    char resultbuf[16];
    size_t resultbuflen = 0;
    int ret = 0;
    uint32_t val32 = 0;
    uint8_t val8 = 0;

    KFS_ENTER();

    if (oper->open == NULL) {
        ret = -ENOSYS;
    } else {
        memset(&ffi, 0, sizeof(ffi));
        memcpy(&val32, rawop, 4);
        memcpy(&val8, rawop + 4, 1);
        ffi.flags = ntohl(val32);
        ffi.direct_io = (val8 >> 0) & 1;
        ffi.keep_cache = (val8 >> 1) & 1;
#if FUSE_VERSION >= 29
        ffi.nonseekable = (val8 >> 2) & 1;
#else
        if ((val8 >> 2) & 1) {
            KFS_INFO("Flag 'nonseekable' set during open request, not supported"
                     " by this server's FUSE library. Please upgrade to >=2.9");
            ret = -ENOTSUP;
        } else
#endif
        ret = oper->open(rawop + 5, &ffi);
    }
    KFS_ASSERT(ret <= 0);
    val32 = htonl(-ret);
    memcpy(resultbuf, &val32, 4);
    if (ret == 0) {
        /* Success: send back the (raw) filehandle. */
        resultbuflen = 16;
        memcpy(resultbuf + 8, &ffi.fh, 8);
    } else {
        resultbuflen = 8;
    }
    /* The length of the result body. */
    val32 = htonl(resultbuflen - 8);
    memcpy(resultbuf + 4, &val32, 4);
    ret = send_msg(c, resultbuf, resultbuflen);

    KFS_RETURN(ret);
}

/**
 * Handle a read operation. The argument message is built up as follows:
 *
 * - filehandle (8 bytes).
 * - number of bytes requested (4 bytes, network order).
 * - offset in the file (8 bytes, network order).
 *
 * The return message is the contents of the file.
 */
static int
handle_read(client_t c, const char *rawop, size_t opsize)
{
    (void) opsize;

    struct fuse_file_info ffi;
    char *resultbuf = NULL;
    uint64_t offset = 0;
    size_t len = 0;
    int ret = 0;
    uint32_t val32 = 0;

    KFS_ENTER();

    if (oper->read == NULL) {
        ret = -ENOSYS;
    } else {
        memset(&ffi, 0, sizeof(ffi));
        memcpy(&ffi.fh, rawop, 8);
        memcpy(&val32, rawop + 8, 4);
        len = ntohl(val32);
        memcpy(&offset, rawop + 12, 1);
        offset = ntohll(offset);
        resultbuf = KFS_MALLOC(len + 8);
        if (resultbuf == NULL) {
            ret = -ENOBUFS;
        } else {
            ret = oper->read(NULL, resultbuf + 8, len, offset, &ffi);
        }
    }
    /* This will fail. TODO: Fix the return value serialisation. */
    KFS_ASSERT(ret <= 0);
    val32 = htonl(-ret);
    memcpy(resultbuf, &val32, 4);
    if (ret < 0) {
        len = 8;
        val32 = htonl(0);
    } else {
        len = ret + 8;
        val32 = htonl(ret);
    }
    /* The length of the result body. */
    memcpy(resultbuf + 4, &val32, 4);
    ret = send_msg(c, resultbuf, len);

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
    [KFS_OPID_READLINK] = handle_readlink,
    [KFS_OPID_MKNOD] = handle_mknod,
    [KFS_OPID_MKDIR] = handle_mkdir,
    [KFS_OPID_UNLINK] = handle_unlink,
    [KFS_OPID_RMDIR] = handle_rmdir,
    [KFS_OPID_SYMLINK] = handle_symlink,
    [KFS_OPID_RENAME] = handle_rename,
    [KFS_OPID_LINK] = handle_link,
    [KFS_OPID_CHMOD] = handle_chmod,
    [KFS_OPID_CHOWN] = handle_chown,
    [KFS_OPID_TRUNCATE] = handle_truncate,
    [KFS_OPID_OPEN] = handle_open,
    [KFS_OPID_READ] = handle_read,
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
