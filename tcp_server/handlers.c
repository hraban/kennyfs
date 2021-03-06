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
/* Required for get[gup]id(). */
#include <sys/types.h>
#include <unistd.h>

#include "kfs_misc.h"
#include "kfs_memory.h"
#include "tcp_brick/tcp_brick.h"
#include "tcp_server/server.h"

static const struct kfs_operations *oper = NULL;
/** State of the subvolume as returned by init(). */
static void *private_data = NULL;
/**
 * Number of bytes to allocate, initially, during opendir(), for the next
 * readdir(). If during readdir() it turns out more space is needed, more space
 * will be allocated dynamically.
 */
static const unsigned int READDIRBUF_SIZE = 1000000;

/** State information used by readdir. */
struct _readdir_fh_t {
    off_t used;
    char *buf;
    char *_realbuf;
    size_t size;
};
typedef struct _readdir_fh_t readdir_fh_t;

/** File handle for directory operations. */
struct _dirfh_t {
    /** File handle struct from FUSE, passed to backend. */
    struct fuse_file_info ffi;
    readdir_fh_t readdir;
};
typedef struct _dirfh_t dirfh_t;

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
 * TODO: This could just as well be used to send back any return value,
 * including 0 (so that the caller does not need to pass some place-holder
 * buffer).
 */
static int
report_error(client_t c, int error)
{
    char resultbuf[8];
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(error >= 0);
    KFS_INFO("An operation failed, sending error %d to client: %s", error,
            strerror(error));
    ret = send_reply(c, -error, resultbuf, 0);

    KFS_RETURN(ret);
}

/**
 * Unserialise an array of 2 struct timespec elements from an array of 4
 * uint64_t elements. Returns a pointer to the array. Total size in bytes: 32.
 *
 * The elements were ordered as follows:
 *
 * [0].tv_sec
 * [0].tv_nsec
 * [1].tv_sec
 * [1].tv_nsec
 */
static struct timespec *
unserialise_timespec(struct timespec tvnano[2], const uint64_t intbuf[4])
{
    KFS_ENTER();

    tvnano[0].tv_sec = ntohll(intbuf[0]);
    tvnano[0].tv_nsec = ntohll(intbuf[1]);
    tvnano[1].tv_sec = ntohll(intbuf[2]);
    tvnano[1].tv_nsec = ntohll(intbuf[3]);

    KFS_RETURN(tvnano);
}

/**
 * Initialise a context for the calling handler. Reentrant, does not fail.
 * Returns the context.
 */
static kfs_context_t
kfs_init_context(kfs_context_t context)
{
    KFS_ENTER();

    context->uid = getuid();
    context->gid = getgid();
    context->priv = private_data;

    KFS_RETURN(context);
}

/**
 * Handle a getattr operation. The argument message is the raw pathname. The
 * return message is a struct stat serialised by the serialise_stat() routine.
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
    struct kfs_context context;

    KFS_ENTER();

    kfs_init_context(&context);
    ret = oper->getattr(&context, rawop, &stbuf);
    KFS_ASSERT(ret <= 0);
    if (ret == 0) {
        /* Call succeeded, also send the body. */
        bodysize = sizeof(intbuf);
        serialise_stat(intbuf, &stbuf);
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
    struct kfs_context context;

    KFS_ENTER();

    kfs_init_context(&context);
    ret = oper->readlink(&context, rawop, resultbuf + 8, sizeof(resultbuf) - 8);
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
    struct kfs_context context;

    KFS_ENTER();

    kfs_init_context(&context);
    memcpy(&mode_serialised, rawop, 4);
    KFS_ASSERT(sizeof(uint32_t) >= sizeof(mode_t));
    mode = ntohl(mode_serialised); 
    ret = oper->mknod(&context, rawop + 4, mode, 0);
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
    struct kfs_context context;

    KFS_ENTER();

    kfs_init_context(&context);
    memcpy(&mode_serialised, rawop, 4);
    KFS_ASSERT(sizeof(uint32_t) >= sizeof(mode_t));
    mode = ntohl(mode_serialised); 
    ret = oper->mkdir(&context, rawop + 4, mode);
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
    struct kfs_context context;

    KFS_ENTER();

    kfs_init_context(&context);
    ret = oper->unlink(&context, rawop);
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
    struct kfs_context context;

    KFS_ENTER();

    kfs_init_context(&context);
    ret = oper->rmdir(&context, rawop);
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
    struct kfs_context context;

    KFS_ENTER();

    kfs_init_context(&context);
    memcpy(&path1len, rawop, 4);
    path1len = ntohl(path1len);
    /* Check that the paths are separated by a '\0'-byte. */
    if (rawop[4 + path1len] != '\0') {
        report_error(c, EINVAL);
        KFS_RETURN(-1);
    }
    path1 = rawop + 4;
    path2 = rawop + 4 + path1len + 1;
    ret = oper->symlink(&context, path1, path2);
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
    struct kfs_context context;

    KFS_ENTER();

    kfs_init_context(&context);
    memcpy(&path1len, rawop, 4);
    path1len = ntohl(path1len);
    /* Check that the paths are separated by a '\0'-byte. */
    if (rawop[4 + path1len] != '\0') {
        report_error(c, EINVAL);
        KFS_RETURN(-1);
    }
    path1 = rawop + 4;
    path2 = rawop + 4 + path1len + 1;
    ret = oper->rename(&context, path1, path2);
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
    struct kfs_context context;

    KFS_ENTER();

    kfs_init_context(&context);
    memcpy(&path1len, rawop, 4);
    path1len = ntohl(path1len);
    /* Check that the paths are separated by a '\0'-byte. */
    if (rawop[4 + path1len] != '\0') {
        report_error(c, EINVAL);
        KFS_RETURN(-1);
    }
    path1 = rawop + 4;
    path2 = rawop + 4 + path1len + 1;
    ret = oper->link(&context, path1, path2);
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
    struct kfs_context context;

    KFS_ENTER();

    kfs_init_context(&context);
    memcpy(&mode_serialised, rawop, 4);
    KFS_ASSERT(sizeof(uint32_t) >= sizeof(mode_t));
    mode = ntohl(mode_serialised); 
    ret = oper->chmod(&context, rawop + 4, mode);
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
    struct kfs_context context;

    KFS_ENTER();

    kfs_init_context(&context);
    memcpy(&uid_serialised, rawop, 4);
    memcpy(&gid_serialised, rawop, 4);
    KFS_ASSERT(sizeof(uint32_t) >= sizeof(uid_t));
    KFS_ASSERT(sizeof(uint32_t) >= sizeof(gid_t));
    uid = ntohl(uid_serialised);
    gid = ntohl(gid_serialised);
    ret = oper->chown(&context, rawop + 8, uid, gid);
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
    struct kfs_context context;

    KFS_ENTER();

    kfs_init_context(&context);
    memcpy(&offset_serialised, rawop, 8);
    KFS_ASSERT(sizeof(uint64_t) >= sizeof(off_t));
    offset = ntohll(offset_serialised);
    ret = oper->truncate(&context, rawop + 8, offset);
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
 *
 * TODO: Guarantee that a release() will follow, also for the backend's sake.
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
    struct kfs_context context;

    KFS_ENTER();

    kfs_init_context(&context);
    memset(&ffi, 0, sizeof(ffi));
    memcpy(&val32, rawop, 4);
    ffi.flags = ntohl(val32);
    ret = oper->open(&context, rawop + 4, &ffi);
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
    struct fuse_file_info ffi;
    char *resultbuf = NULL;
    uint64_t offset = 0;
    size_t len = 0;
    int ret = 0;
    uint32_t val32 = 0;
    struct kfs_context context;

    KFS_ENTER();

    if (opsize != 8 + 4 + 8) {
        report_error(c, EINVAL);
        KFS_RETURN(-1);
    }
    kfs_init_context(&context);
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
        ret = oper->read(&context, NULL, resultbuf + 8, len, offset, &ffi);
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
 * The number of bytes to write is deduced from the total length of the
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
    struct kfs_context context;

    KFS_ENTER();

    kfs_init_context(&context);
    memset(&ffi, 0, sizeof(ffi));
    memcpy(&ffi.fh, rawop, 8);
    writelen = opsize - 16;
    memcpy(&offset, rawop + 8, 8);
    offset = ntohll(offset);
    if (resultbuf == NULL) {
        ret = -ENOBUFS;
    } else {
        ret = oper->write(&context, NULL, rawop + 16, writelen, offset, &ffi);
    }
    ret = send_reply(c, ret, resultbuf, 0);

    KFS_RETURN(ret);
}

/**
 * Handle a flush operation. The argument message consists of the 8 byte
 * filehandle. The return message is empty.
 */
static int
handle_flush(client_t c, const char *rawop, size_t opsize)
{
    char resultbuf[8];
    struct fuse_file_info ffi;
    int ret = 0;
    struct kfs_context context;

    KFS_ENTER();

    if (opsize != 8) {
        report_error(c, EINVAL);
        KFS_RETURN(-1);
    }
    kfs_init_context(&context);
    memcpy(&ffi.fh, rawop, 8);
    ret = oper->flush(&context, NULL, &ffi);
    ret = send_reply(c, ret, resultbuf, 0);

    KFS_RETURN(ret);
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
    struct kfs_context context;

    KFS_ENTER();

    kfs_init_context(&context);
    if (opsize != 8) {
        report_error(c, EINVAL);
        KFS_RETURN(-1);
    }
    memcpy(&ffi.fh, rawop, 8);
    ret = oper->release(&context, NULL, &ffi);
    ret = send_reply(c, ret, resultbuf, 0);

    KFS_RETURN(ret);
}

/**
 * Handle a fsync operation. The argument message consists of the 8 byte
 * filehandle, followed by a one-byte flag for the "datasync" parameter. The
 * return message is empty.
 */
static int
handle_fsync(client_t c, const char *rawop, size_t opsize)
{
    char resultbuf[8];
    struct fuse_file_info ffi;
    int ret = 0;
    struct kfs_context context;

    KFS_ENTER();

    kfs_init_context(&context);
    if (opsize != 9) {
        report_error(c, EINVAL);
        KFS_RETURN(-1);
    }
    memcpy(&ffi.fh, rawop, 8);
    ret = oper->fsync(&context, NULL, rawop[8], &ffi);
    ret = send_reply(c, ret, resultbuf, 0);

    KFS_RETURN(ret);
}

/**
 * Handle a opendir operation. The argument message is the pathname of the
 * directory. The return message is the filehandle that must be supplied with
 * every subsequent operation on this directory (8 bytes).
 *
 * TODO: Guarantee that a releasedir() will follow, also for the backend's sake.
 */
static int
handle_opendir(client_t c, const char *rawop, size_t opsize)
{
    (void) opsize;

    char resultbuf[8 + 8];
    dirfh_t * dirfh = NULL;
    int ret = 0;
    size_t reply_size = 0;
    struct kfs_context context;

    KFS_ENTER();

    kfs_init_context(&context);
    dirfh = KFS_CALLOC(1, sizeof(*dirfh));
    if (dirfh == NULL) {
        report_error(c, ENOMEM);
        KFS_RETURN(-1);
    }
    dirfh->readdir.size = READDIRBUF_SIZE;
    dirfh->readdir.used = 0;
    /* For the motivation behind the + 8, see the readdir() handler. */
    dirfh->readdir._realbuf = KFS_MALLOC(dirfh->readdir.size + 8);
    if (dirfh->readdir._realbuf == NULL) {
        dirfh = KFS_FREE(dirfh);
        report_error(c, ENOMEM);
        KFS_RETURN(-1);
    }
    dirfh->readdir.buf = dirfh->readdir._realbuf + 8;
    ret = oper->opendir(&context, rawop, &dirfh->ffi);
    if (ret != 0) {
        dirfh->readdir._realbuf = KFS_FREE(dirfh->readdir._realbuf);
        dirfh = KFS_FREE(dirfh);
        reply_size = 0;
    } else {
        /* 8 bytes are reserved for the fh. If it is smaller, no problem. */
        KFS_ASSERT(sizeof(dirfh) <= 8);
        memcpy(resultbuf + 8, &dirfh, sizeof(dirfh));
        reply_size = 8;
    }
    ret = send_reply(c, ret, resultbuf, reply_size);

    KFS_RETURN(ret);
}

/**
 * The FUSE API requires a handler to be passed to a readdir() call which will
 * take in directory entries and store them in a buffer. This function is such a
 * handler.
 */
static int
readdir_filler(void *rdfh_, const char *name, const struct stat *stbuf, off_t off)
{
    readdir_fh_t * const rdfh = rdfh_;
    uint32_t intbuf[13];
    uint64_t off_serialised = 0;
    char *buf = NULL;
    size_t namelen = 0;
    size_t newlen = 0;
    uint32_t namelen_serialised = 0;

    KFS_ENTER();

    KFS_ASSERT(rdfh_ != NULL && name != NULL && stbuf != NULL);
    KFS_ASSERT(rdfh->buf != NULL && rdfh->used <= rdfh->size);
    namelen = strlen(name);
    newlen = rdfh->used + sizeof(intbuf) + 4 + 8 + namelen + 1;
    KFS_DEBUG("Adding dir entry %s to buffer at %p. Size: %lu + %lu = %lu.",
            name, rdfh_, (unsigned long) rdfh->used, (unsigned long) (newlen -
            rdfh->used), (unsigned long) newlen);
    if (newlen > rdfh->size) {
        KFS_DEBUG("Never mind, can not grow beyond %llu bytes.",
                (unsigned long long) rdfh->size);
        KFS_RETURN(1);
    }
    buf = rdfh->buf + rdfh->used;
    /* The struct stat. */
    serialise_stat(intbuf, stbuf);
    memcpy(buf, intbuf, sizeof(intbuf));
    buf += sizeof(intbuf);
    /* The offset. */
    off_serialised = htonll(off);
    memcpy(buf, &off_serialised, 8);
    buf += 8;
    /* The length of the name. */
    namelen_serialised = htonl(namelen);
    memcpy(buf, &namelen_serialised, 4);
    buf += 4;
    /* The name itself. */
    memcpy(buf, name, namelen);
    buf += namelen;
    /* The terminating '\0'-byte. */
    *buf = '\0';
    /* Now pointing one byte beyond this entry. */
    buf += 1;
    rdfh->used = newlen;
    KFS_ASSERT(rdfh->buf + rdfh->used == buf);

    KFS_RETURN(0);
}

/**
 * Handle a readdir operation. The argument message consists of the following
 * elements:
 *
 * - file handle (8 bytes)
 * - offset in the directory entries (8 bytes, network order)
 *
 * The return message contains all entries in the given directory, each of them
 * serialised as follows:
 *
 * - a serialised stat struct (see serialise_stat())
 * - the offset as passed to the filler as a uint64_t (network order, 8 bytes)
 * - the length of the entry name as a uint32_t (network order, 4 bytes)
 * - the name of the entry
 * - one '\0' byte as a terminator.
 */
static int
handle_readdir(client_t c, const char *rawop, size_t opsize)
{
    uint64_t off = 0;
    dirfh_t *dirfh = NULL;
    readdir_fh_t *rdfh = NULL;
    int ret = 0;
    struct kfs_context context;

    KFS_ENTER();

    kfs_init_context(&context);
    if (opsize != 16) {
        report_error(c, EINVAL);
        KFS_RETURN(-1);
    }
    memcpy(&dirfh, rawop, sizeof(dirfh));
    rdfh = &(dirfh->readdir);
    memcpy(&off, rawop + 8, 8);
    off = ntohll(off);
    ret = oper->readdir(&context, NULL, rdfh, readdir_filler, off, &(dirfh->ffi));
    /*
     * This is where the 8 hidden leading allocated bytes in the buffer come in
     * handy:
     */
    KFS_DEBUG("Completed readdir call, sending back %lu bytes.", (unsigned long)
            rdfh->used);
    ret = send_reply(c, ret, rdfh->_realbuf, rdfh->used);
    /* Flush the buffer. */
    rdfh->used = 0;

    KFS_RETURN(ret);
}

/**
 * Handle a releasedir operation. The argument message consists of the 8 byte
 * filehandle. The return message is empty.
 */
static int
handle_releasedir(client_t c, const char *rawop, size_t opsize)
{
    char resultbuf[8];
    dirfh_t *dirfh = NULL;
    int ret = 0;
    struct kfs_context context;

    KFS_ENTER();

    kfs_init_context(&context);
    if (opsize != 8) {
        report_error(c, EINVAL);
        KFS_RETURN(-1);
    }
    memcpy(&dirfh, rawop, sizeof(dirfh));
    ret = oper->releasedir(&context, NULL, &(dirfh->ffi));
    dirfh->readdir._realbuf = KFS_FREE(dirfh->readdir._realbuf);
    dirfh->readdir.buf = NULL;
    dirfh = KFS_FREE(dirfh);
    ret = send_reply(c, ret, resultbuf, 0);

    KFS_RETURN(ret);
}

/**
 * Handle a create operation. The argument message consists of argument flags
 * (serialised as a uint32_t, network order) followed by the mode (serialised as
 * a uint32_t), followed by the pathname.
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
 *
 * TODO: Guarantee that a release() will follow, also for the backend's sake.
 */
static int
handle_create(client_t c, const char *rawop, size_t opsize)
{
    (void) opsize;

    struct fuse_file_info ffi;
    char resultbuf[17];
    size_t bodysize = 0;
    int ret = 0;
    uint32_t val32 = 0;
    mode_t mode = 0;
    struct kfs_context context;

    KFS_ENTER();

    kfs_init_context(&context);
    memset(&ffi, 0, sizeof(ffi));
    memcpy(&val32, rawop, 4);
    ffi.flags = ntohl(val32);
    memcpy(&val32, rawop + 4, 4);
    mode = ntohl(val32);
    ret = oper->create(&context, rawop + 8, mode, &ffi);
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
 * Handle a fgetattr operation. The argument message is the filehandle. The
 * return message is a struct stat serialised by the serialise_stat() routine.
 * (See handle_getattr()).
 */
static int
handle_fgetattr(client_t c, const char *rawop, size_t opsize)
{
    uint32_t intbuf[13];
    char resbuf[8 + sizeof(intbuf)];
    struct fuse_file_info ffi;
    struct stat stbuf;
    size_t bodysize = 0;
    int ret = 0;
    struct kfs_context context;

    KFS_ENTER();

    if (opsize != 8) {
        report_error(c, EINVAL);
        KFS_RETURN(-1);
    }
    kfs_init_context(&context);
    memset(&ffi, 0, sizeof(ffi));
    memcpy(&ffi.fh, rawop, 8);
    ret = oper->fgetattr(&context, NULL, &stbuf, &ffi);
    if (ret == 0) {
        /* Call succeeded, also send the body. */
        bodysize = sizeof(intbuf);
        serialise_stat(intbuf, &stbuf);
        memcpy(resbuf + 8, intbuf, bodysize);
    } else {
        /* Call failed, return only the error code. */
        bodysize = 0;
    }
    ret = send_reply(c, ret, resbuf, bodysize);

    KFS_RETURN(ret);
}

/**
 * Handle a utimens operation. The argument is the serialised new file
 * modification and access times (see unserialise_timespec) followed by the
 * pathname. The return message is empty (just the error code).
 */
static int
handle_utimens(client_t c, const char *rawop, size_t opsize)
{
    uint64_t intbuf[4];
    char resbuf[8];
    struct timespec tvnano[2];
    int ret = 0;
    struct kfs_context context;

    KFS_ENTER();

    kfs_init_context(&context);
    if (opsize < sizeof(intbuf)) {
        report_error(c, EINVAL);
        KFS_RETURN(-1);
    }
    memcpy(intbuf, rawop, sizeof(intbuf));
    unserialise_timespec(tvnano, intbuf);
    ret = oper->utimens(&context, rawop + sizeof(intbuf), tvnano);
    ret = send_reply(c, ret, resbuf, 0);

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
    [KFS_OPID_WRITE] = handle_write,
    [KFS_OPID_STATFS] = NULL,
    [KFS_OPID_FLUSH] = handle_flush,
    [KFS_OPID_RELEASE] = handle_release,
    [KFS_OPID_FSYNC] = handle_fsync,
    [KFS_OPID_SETXATTR] = NULL,
    [KFS_OPID_GETXATTR] = NULL,
    [KFS_OPID_LISTXATTR] = NULL,
    [KFS_OPID_REMOVEXATTR] = NULL,
    [KFS_OPID_OPENDIR] = handle_opendir,
    [KFS_OPID_READDIR] = handle_readdir,
    [KFS_OPID_RELEASEDIR] = handle_releasedir,
    [KFS_OPID_FSYNCDIR] = NULL,
    [KFS_OPID_ACCESS] = NULL,
    [KFS_OPID_CREATE] = handle_create,
    [KFS_OPID_FTRUNCATE] = NULL,
    [KFS_OPID_FGETATTR] = handle_fgetattr,
    [KFS_OPID_LOCK] = NULL,
    [KFS_OPID_UTIMENS] = handle_utimens,
    [KFS_OPID_BMAP] = NULL,
#if FUSE_VERSION >= 28
    [KFS_OPID_IOCTL] = NULL,
    [KFS_OPID_POLL] = NULL,
#endif
    [KFS_OPID_QUIT] = handle_quit,
};

void
init_handlers(const struct kfs_operations *kfs_oper, void *subv_state)
{
    KFS_ENTER();

    oper = kfs_oper;
    private_data = subv_state;

    KFS_RETURN();
}

const handler_t *
get_handlers(void)
{
    KFS_ENTER();

    KFS_RETURN(handlers);
}
