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
 * Send a reply to given client. The return value is serialised according to the
 * protocol and the size of the reply is embedded in the header as well. The
 * buffer should be able to contain the entire message, header and body, and the
 * body should be filled out completely already. Because the header is 8 bytes
 * long, the buffer must be of a size at least 8 bytes larger than the indicated
 * size of the body, and the body must start at an offset of 8 bytes, not at 0.
 *
 * It would be prettier to just accept a buffer for the body and allocate memory
 * for a fresh new buffer, fill that with the necessary data and not have the
 * caller worry about the size of the header, but that would require a new
 * malloc for every reply, which I try to avoid.
 *
 * Returns -1 on failure, 0 on succesful sending.
 */
static int
send_reply(client_t c, int returnvalue, char *buf, size_t bodysize)
{
    uint32_t val32 = 0;
    int ret = 0;

    KFS_ENTER();

    /* This assertion can not be checked by the compiler but it must hold. */
    // KFS_ASSERT(NUMELEM(buf) >= bodysize + 8);
    /* Return value. */
    val32 = htonl(returnvalue + (1 << 31));
    memcpy(buf, &val32, 4);
    /* Size of the body. */
    val32 = htonl(bodysize);
    memcpy(buf + 4, &val32, 4);
    ret = send_msg(c, buf, bodysize + 8);

    KFS_RETURN(ret);
}

/**
 * Indicates to the client that the operation it sent was invalid / corrupt.
 */
static int
report_invalid(client_t c)
{
    char resultbuf[8];
    int ret = 0;

    KFS_ENTER();

    KFS_INFO("Received invalid / corrupt request from client.");
    ret = send_reply(c, -EINVAL, resultbuf, 0);

    KFS_RETURN(ret);
}

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
    size_t bodysize = 0;
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
        bodysize = sizeof(intbuf);
        memcpy(resbuf + 8, intbuf, bodysize);
    } else {
        /* Call failed, return only the error code. */
        bodysize = 0;
    }
    ret = send_reply(c, ret, resbuf, bodysize);

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

    int ret = 0;
    size_t bodysize = 0;
    char resultbuf[PATHBUF_SIZE + 8];

    KFS_ENTER();

    if (oper->readlink == NULL) {
        ret = -ENOSYS;
    } else {
        ret = oper->readlink(rawop, resultbuf + 8, sizeof(resultbuf) - 8);
    }
    KFS_ASSERT(ret <= 0);
    if (ret == 0) {
        bodysize = strlen(resultbuf + 8);
    } else {
        bodysize = 0;
    }
    ret = send_reply(c, ret, resultbuf, bodysize);

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
    ret = send_reply(c, ret, resultbuf, 0);

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
    ret = send_reply(c, ret, resultbuf, 0);

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
    int ret = 0;

    KFS_ENTER();

    if (oper->unlink == NULL) {
        ret = -ENOSYS;
    } else {
        ret = oper->unlink(rawop);
    }
    KFS_ASSERT(ret <= 0);
    ret = send_reply(c, ret, resultbuf, 0);

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
    int ret = 0;

    KFS_ENTER();

    if (oper->rmdir == NULL) {
        ret = -ENOSYS;
    } else {
        ret = oper->rmdir(rawop);
    }
    KFS_ASSERT(ret <= 0);
    ret = send_reply(c, ret, resultbuf, 0);

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
    uint32_t path1len = 0;
    const char *path1 = NULL;
    const char *path2 = NULL;
    int ret = 0;

    KFS_ENTER();

    if (oper->symlink == NULL) {
        ret = -ENOSYS;
    } else {
        memcpy(&path1len, rawop, 4);
        path1len = ntohl(path1len);
        /* Check that the paths are separated by a '\0'-byte. */
        if (rawop[4 + path1len] != '\0') {
            report_invalid(c);
            KFS_RETURN(-1);
        }
        path1 = rawop + 4;
        path2 = rawop + 4 + path1len + 1;
        ret = oper->symlink(path1, path2);
    }
    KFS_ASSERT(ret <= 0);
    ret = send_reply(c, ret, resultbuf, 0);

    KFS_RETURN(ret);
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
    uint32_t path1len = 0;
    const char *path1 = NULL;
    const char *path2 = NULL;
    int ret = 0;

    KFS_ENTER();

    if (oper->rename == NULL) {
        ret = -ENOSYS;
    } else {
        memcpy(&path1len, rawop, 4);
        path1len = ntohl(path1len);
        /* Check that the paths are separated by a '\0'-byte. */
        if (rawop[4 + path1len] != '\0') {
            report_invalid(c);
            KFS_RETURN(-1);
        }
        path1 = rawop + 4;
        path2 = rawop + 4 + path1len + 1;
        ret = oper->rename(path1, path2);
    }
    KFS_ASSERT(ret <= 0);
    ret = send_reply(c, ret, resultbuf, 0);

    KFS_RETURN(ret);
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
    uint32_t path1len = 0;
    const char *path1 = NULL;
    const char *path2 = NULL;
    int ret = 0;

    KFS_ENTER();

    if (oper->link == NULL) {
        ret = -ENOSYS;
    } else {
        memcpy(&path1len, rawop, 4);
        path1len = ntohl(path1len);
        /* Check that the paths are separated by a '\0'-byte. */
        if (rawop[4 + path1len] != '\0') {
            report_invalid(c);
            KFS_RETURN(-1);
        }
        path1 = rawop + 4;
        path2 = rawop + 4 + path1len + 1;
        ret = oper->link(path1, path2);
    }
    KFS_ASSERT(ret <= 0);
    ret = send_reply(c, ret, resultbuf, 0);

    KFS_RETURN(ret);
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
    ret = send_reply(c, ret, resultbuf, 0);

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
    ret = send_reply(c, ret, resultbuf, 0);

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
    ret = send_reply(c, ret, resultbuf, 0);

    KFS_RETURN(ret);
}

/**
 * Handle an open operation. The argument message consists of argument flags
 * (serialised as a uint32_t, network order) followed by the pathname.
 *
 * The return message is an 8-byte character array that will be used to uniquely
 * identify this file in further operations (a file handler). It is, in fact,
 * the fh element, but that is an implementation detail. This 8-byte identifier
 * is followed by three flags, stored in one byte (total response: 9 bytes),
 * from lsb to msb: direct_io (0), keep_cache (1), nonseekable (2, only if FUSE
 * API version >= 2.9).
 *
 * TODO: The flags element is an int in the original struct, which is larger
 * than a uint32_t on some architectures. Can that become a problem?
 */
static int
handle_open(client_t c, const char *rawop, size_t opsize)
{
    (void) opsize;

    struct fuse_file_info ffi;
    char resultbuf[17];
    size_t bodysize = 0;
    int ret = 0;
    uint32_t val32 = 0;

    KFS_ENTER();

    if (oper->open == NULL) {
        ret = -ENOSYS;
    } else {
        memset(&ffi, 0, sizeof(ffi));
        memcpy(&val32, rawop, 4);
        ffi.flags = ntohl(val32);
        ret = oper->open(rawop + 4, &ffi);
    }
    KFS_ASSERT(ret <= 0);
    if (ret == 0) {
        /* Success: send back the (raw) filehandle. */
        bodysize = 9;
        memcpy(resultbuf + 8, &ffi.fh, 8);
        resultbuf[16] = (ffi.direct_io << 0) | (ffi.keep_cache << 1);
#if FUSE_VERSION >= 29
        resultbuf[16] |= ffi.non_seekable << 2;
#endif
    } else {
        bodysize = 0;
    }
    ret = send_reply(c, ret, resultbuf, bodysize);

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
        memcpy(&offset, rawop + 12, 8);
        offset = ntohll(offset);
        resultbuf = KFS_MALLOC(len + 8);
        if (resultbuf == NULL) {
            ret = -ENOBUFS;
        } else {
            ret = oper->read(NULL, resultbuf + 8, len, offset, &ffi);
        }
    }
    if (ret < 0) {
        /* The length of the result body. */
        len = 0;
    } else {
        len = ret;
    }
    ret = send_reply(c, ret, resultbuf, len);
    resultbuf = KFS_FREE(resultbuf);

    KFS_RETURN(ret);
}

/**
 * Handle a write operation. The argument message is built up as follows:
 *
 * - filehandle (8 bytes).
 * - offset in the file (8 bytes, network order).
 * - data to write.
 *
 * The number of bytes to write is deducted from the total length of the
 * message.  The return message is empty.
 */
static int
handle_write(client_t c, const char *rawop, size_t opsize)
{
    char resultbuf[8];
    struct fuse_file_info ffi;
    uint64_t offset = 0;
    int ret = 0;
    size_t writelen = 0;

    KFS_ENTER();

    if (oper->write == NULL) {
        ret = -ENOSYS;
    } else {
        memset(&ffi, 0, sizeof(ffi));
        memcpy(&ffi.fh, rawop, 8);
        writelen = opsize - 20;
        memcpy(&offset, rawop + 8, 8);
        offset = ntohll(offset);
        if (resultbuf == NULL) {
            ret = -ENOBUFS;
        } else {
            ret = oper->write(NULL, rawop + 16, writelen, offset, &ffi);
        }
    }
    ret = send_reply(c, ret, resultbuf, 0);

    KFS_RETURN(0);
}

/**
 * Handle a release operation. The argument message consists of the 8 byte
 * filehandle. The return message is empty.
 */
static int
handle_release(client_t c, const char *rawop, size_t opsize)
{
    char resultbuf[8];
    struct fuse_file_info ffi;
    int ret = 0;

    KFS_ENTER();

    if (opsize != 8) {
        report_invalid(c);
        KFS_RETURN(-1);
    }
    if (oper->release == NULL) {
        ret = -ENOSYS;
    } else {
        memcpy(&ffi.fh, rawop, 8);
        ret = oper->release(NULL, &ffi);
    }
    ret = send_reply(c, ret, resultbuf, 0);

    KFS_RETURN(0);
}

/**
 * Handle a fsync operation. The argument message consists of the 8 byte
 * filehandle, followed by a one-byte flag for the "datasync" parameter. The
 * return message is empty.
 */
static int
handle_fsync(client_t c, const char *rawop, size_t opsize)
{
    char resultbuf[9];
    struct fuse_file_info ffi;
    int ret = 0;

    KFS_ENTER();

    if (opsize != 9) {
        report_invalid(c);
        KFS_RETURN(-1);
    }
    if (oper->fsync == NULL) {
        ret = -ENOSYS;
    } else {
        memcpy(&ffi.fh, rawop, 8);
        ret = oper->fsync(NULL, rawop[8], &ffi);
    }
    ret = send_reply(c, ret, resultbuf, 0);

    KFS_RETURN(0);
}

/**
 * Handle a opendir operation. The argument message is the pathname of the
 * directory. The return message is the filehandle that must be supplied with
 * every subsequent operation on this directory (8 bytes).
 */
static int
handle_opendir(client_t c, const char *rawop, size_t opsize)
{
    (void) opsize;

    char resultbuf[16];
    struct fuse_file_info ffi;
    int ret = 0;

    KFS_ENTER();

    if (oper->opendir == NULL) {
        ret = -ENOSYS;
    } else {
        memcpy(&ffi.fh, rawop, 8);
        ret = oper->opendir(rawop, &ffi);
    }
    memcpy(resultbuf + 8, &ffi.fh, 8);
    ret = send_reply(c, ret, resultbuf, 8);

    KFS_RETURN(0);
}

/**
 * Handle a releasedir operation. The argument message consists of the 8 byte
 * filehandle. The return message is empty.
 */
static int
handle_releasedir(client_t c, const char *rawop, size_t opsize)
{
    char resultbuf[8];
    struct fuse_file_info ffi;
    int ret = 0;

    KFS_ENTER();

    if (opsize != 8) {
        report_invalid(c);
        KFS_RETURN(-1);
    }
    if (oper->releasedir == NULL) {
        ret = -ENOSYS;
    } else {
        memcpy(&ffi.fh, rawop, 8);
        ret = oper->releasedir(NULL, &ffi);
    }
    ret = send_reply(c, ret, resultbuf, 0);

    KFS_RETURN(0);
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
    [KFS_OPID_WRITE] = handle_write,
    [KFS_OPID_STATFS] = NULL,
    [KFS_OPID_FLUSH] = NULL, /* TODO: Check if this needs a handler. */
    [KFS_OPID_RELEASE] = handle_release,
    [KFS_OPID_FSYNC] = handle_fsync,
    [KFS_OPID_OPENDIR] = handle_opendir,
    [KFS_OPID_READDIR] = NULL,
    [KFS_OPID_RELEASEDIR] = handle_releasedir,
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
