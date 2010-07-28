/**
 * Handlers for operations passed down to the kennyfs TCP brick. Documentation
 * for the format of operation messages and their return counterparts can be
 * found in the TCP brick server documentation.
 *
 * Most calls to the pthread library are followed by a KFS_ASSERT(return_value
 * == 0);. This is done because these calls are simply expected to not fail: if
 * they do, no error checking will help because the only solution would be to
 * break down and crash, anyway. The odds are considered not worth the proper
 * error messaging.
 */

#define FUSE_USE_VERSION 29

#include "tcp_brick/handlers.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fuse.h>
#include <fuse_opt.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

#include "kfs.h"
#include "kfs_misc.h"
#include "tcp_brick/connection.h"
#include "tcp_brick/tcp_brick.h"

/**
 * Size of the buffer that will hold the reply (from the server) to a readdir
 * operation.
 */
const size_t READDIR_BUFSIZE = 100000;
/**
 * Worker thread that handles the connection with the server. It passes
 * serialised operations over to the connection module and waits for their
 * reply. The reason for this separate thread's existence is that it may return
 * control of the main thread back as soon as the reply to the actual operation
 * comes in while continuing with other activities in the background
 * (`prefetching').
 */
static pthread_t connection_thread;
/**
 * Fixed location for argument passing between connection thread and handler
 * thread(s).
 */
struct serialised_operation *putop = NULL;
pthread_mutex_t putop_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t putop_cond = PTHREAD_COND_INITIALIZER;

/**
 * Wrapper around the do_operation() routine from tcp_brick/connection.c.
 * Expects a buffer filled with `size' bytes, but starting at element 6 (not 0),
 * meaning that the buffer is in fact 6 bytes bigger than `size'.  This wrapper
 * will fill in the first six bytes as per the protocol. The return buffers are
 * handled just as by do_operation(). On failure by the client (i.e.: by
 * do_operation) -EREMOTEIO is returned, otherwise any return value received
 * from the server is directly returned. This means that it is impossible to
 * distinguish between a local and a remote EREMOTEIO, except by having a look
 * at the logs. The realresbufsize argument is set to the actual size of the
 * result message (0 on error). However, if it is NULL, the caller is expected
 * to be certain of the result size: if it differs from resbufsize, it is
 * considered an error.
 */
static int
do_operation_wrapper(enum fuse_op_id id, char *operbuf, size_t operbufsize,
                     char *resbuf, size_t resbufsize, size_t *realresbufsize)
{
    struct serialised_operation arg = {
        .done_cond = PTHREAD_COND_INITIALIZER,
        .done_mutex = PTHREAD_MUTEX_INITIALIZER};
    int ret = 0;
    uint32_t size_serialised = 0;
    uint16_t id_serialised = 0;

    KFS_ENTER();

    KFS_ASSERT(operbuf != NULL);
    /* If caller is not interested in last argument, store it in local var. */
    size_serialised = htonl(operbufsize);
    id_serialised = htons(id);
    memcpy(operbuf, &size_serialised, 4);
    memcpy(operbuf + 4, &id_serialised, 2);
    operbufsize += 6;
    /* Pack all arguments into a struct. */
    arg.id = id;
    arg.operbuf = operbuf;
    arg.operbufsize = operbufsize;
    arg.resbuf = resbuf;
    arg.resbufsize = resbufsize;
    /* Set up the argument so that we can wait for its completion. */
    ret = pthread_mutex_lock(&arg.done_mutex); KFS_ASSERT(ret == 0);
    for (;;) {
        /* Acquire the mutex before messing with globals. */
        ret = pthread_mutex_lock(&putop_mutex); KFS_ASSERT(ret == 0);
        if (putop == NULL) {
            break;
        }
        /*
         * Somebody has already put an operation in queue. The fact that control
         * reached this point means this thread got the mutex before the handler
         * thread did (a rare race condition, because that is the first thing it
         * does). Give it some more time to acquire that mutex, handle the
         * operation and then try again.
         */
        ret = pthread_mutex_unlock(&putop_mutex); KFS_ASSERT(ret == 0);
        KFS_SLEEP(1);
    }
    /* Set the operation as being "next-in-line". */
    putop = &arg;
    /* Tell the operation handler a new one is available. */
    ret = pthread_cond_signal(&putop_cond); KFS_ASSERT(ret == 0);
    ret = pthread_mutex_unlock(&putop_mutex); KFS_ASSERT(ret == 0);
    /* Wait for the operation to be handled. */
    ret = pthread_cond_wait(&arg.done_cond, &arg.done_mutex); KFS_ASSERT(ret ==
            0);
    /*
     * The operation is done. Leave the arg.done_mutex locked to prevent
     * accidental reuse.
     */
    if (arg.clientret == -1) {
        /* Client side failure. */
        if (realresbufsize != NULL) {
            *realresbufsize = 0;
        }
        KFS_RETURN(-EREMOTEIO);
    }
    if (realresbufsize != NULL) {
        *realresbufsize = arg.resbufused;
    } else if (arg.serverret >= 0 && arg.resbufsize != arg.resbufused) {
        KFS_WARNING("Incoming message size (%lu) is not as expected (%lu).",
                (unsigned long) arg.resbufused, (unsigned long) arg.resbufsize);
        KFS_RETURN(-EREMOTEIO);
    }
    if (arg.serverret < 0) {
        KFS_INFO("Remote side responded to operation %u with error %d: %s.",
                (unsigned int) id, arg.serverret, strerror(-arg.serverret));
    }

    KFS_RETURN(arg.serverret);
}

/**
 * Counterpart to tcp_server/handlers.c's serialise_stat().
 */
static struct stat *
unserialise_stat(struct stat *stbuf, const uint32_t *intbuf)
{
    KFS_ENTER();

    KFS_ASSERT(stbuf != NULL && intbuf != NULL);
    KFS_ASSERT(sizeof(uint32_t) == 4);
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

    KFS_RETURN(stbuf);
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
            sizeof(resbuf), NULL);
    operbuf = KFS_FREE(operbuf);
    if (ret != 0) {
        KFS_RETURN(ret);
    }
    KFS_ASSERT(sizeof(intbuf) == sizeof(resbuf));
    memcpy(intbuf, resbuf, sizeof(intbuf));
    stbuf = unserialise_stat(stbuf, intbuf);

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
    /* Account for the final \0 byte. */
    ret = do_operation_wrapper(KFS_OPID_READLINK, operbuf, pathlen, buf,
            size - 1, &size);
    if (ret == 0) {
        buf[size] = '\0';
    }
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
    ret = do_operation_wrapper(KFS_OPID_MKNOD, operbuf, pathlen + 4, NULL, 0,
            NULL);
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
    ret = do_operation_wrapper(KFS_OPID_MKDIR, operbuf, pathlen + 4, NULL, 0,
            NULL);
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
    ret = do_operation_wrapper(KFS_OPID_UNLINK, operbuf, pathlen, NULL, 0,
            NULL);
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
    ret = do_operation_wrapper(KFS_OPID_RMDIR, operbuf, pathlen, NULL, 0, NULL);
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
    ret = do_operation_wrapper(KFS_OPID_SYMLINK, operbuf, opersize, NULL, 0,
            NULL);
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
    ret = do_operation_wrapper(KFS_OPID_RENAME, operbuf, opersize, NULL, 0,
            NULL);
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
    ret = do_operation_wrapper(KFS_OPID_LINK, operbuf, opersize, NULL, 0, NULL);
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
    ret = do_operation_wrapper(KFS_OPID_CHMOD, operbuf, pathlen + 4, NULL, 0,
            NULL);
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
    ret = do_operation_wrapper(KFS_OPID_CHOWN, operbuf, pathlen + 8, NULL, 0,
            NULL);
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
            0, NULL);
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
    operbuf = KFS_MALLOC(pathlen + 4 + 6);
    if (operbuf == NULL) {
        KFS_RETURN(-ENOMEM);
    }
    flags_serialised = htonl(ffi->flags);
    memcpy(operbuf + 6, &flags_serialised, 4);
    memcpy(operbuf + 10, path, pathlen);
    ret = do_operation_wrapper(KFS_OPID_OPEN, operbuf, pathlen + 4, resbuf,
            sizeof(resbuf), NULL);
    operbuf = KFS_FREE(operbuf);
    if (ret == 0) {
        KFS_ASSERT(sizeof(ffi->fh) == 8);
        memcpy(&(ffi->fh), resbuf, 8);
        ffi->direct_io = (resbuf[8] << 0) & 1;
        ffi->keep_cache = (resbuf[8] << 1) & 1;
#if FUSE_VERSION >= 29
        ffi->nonseekable = (resbuf[8] << 2) & 1;
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
    ret = do_operation_wrapper(KFS_OPID_READ, operbuf, 20, buf, nbyte, &nbyte);
    /* On success, the result value is the number of bytes read. */
    KFS_ASSERT(ret < 0 || ret == nbyte);

    KFS_RETURN(ret);
}

static int
kenny_write(const char *path, const char *buf, size_t nbyte, off_t offset,
        struct fuse_file_info *ffi)
{
    (void) path;

    uint64_t val64 = 0;
    char *operbuf = NULL;
    const size_t bodylen = 8 + 8 + nbyte;
    int ret = 0;

    KFS_ENTER();

    operbuf = KFS_MALLOC(bodylen + 6);
    if (operbuf == NULL) {
        KFS_RETURN(-ENOMEM);
    }
    /* The file handle. */
    memcpy(operbuf + 6, &ffi->fh, 8);
    /* The offset in the file. */
    val64 = htonll(offset);
    memcpy(operbuf + 14, &val64, 8);
    memcpy(operbuf + 22, buf, nbyte);
    ret = do_operation_wrapper(KFS_OPID_WRITE, operbuf, bodylen, NULL, 0, NULL);
    operbuf = KFS_FREE(operbuf);

    KFS_RETURN(ret);
}

static int
kenny_release(const char *path, struct fuse_file_info *ffi)
{
    (void) path;

    char operbuf[8 + 6];
    int ret = 0;

    KFS_ENTER();

    /* The file handle. */
    memcpy(operbuf + 6, &ffi->fh, 8);
    ret = do_operation_wrapper(KFS_OPID_RELEASE, operbuf, 8, NULL, 0, NULL);

    KFS_RETURN(ret);
}

static int
kenny_opendir(const char *path, struct fuse_file_info *ffi)
{
    void * const fh = &ffi->fh;
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
    ret = do_operation_wrapper(KFS_OPID_OPENDIR, operbuf, pathlen, fh, 8, NULL);
    operbuf = KFS_FREE(operbuf);

    KFS_RETURN(ret);
}

/**
 * Given the server's reply to a readdir operation (resbuf), take the first
 * directory entry and add it to the fuse buffer with the supplied filler
 * function. Returns the number of bytes that were advanced in the buffer, or
 * zero if the buffer was full.
 */
static size_t
extract_dirent(char *resbuf, void *fusebuf, fuse_fill_dir_t filler)
{
    uint32_t intbuf[13];
    struct stat stbuf;
    uint64_t offset = 0;
    char *buf = NULL;
    uint32_t namelen = 0;
    int ret = 0;

    KFS_ENTER();

    buf = resbuf;
    memcpy(intbuf, resbuf, sizeof(intbuf));
    unserialise_stat(&stbuf, intbuf);
    buf += sizeof(intbuf);
    memcpy(&offset, buf, 8);
    offset = ntohll(offset);
    buf += 8;
    memcpy(&namelen, buf, 4);
    namelen = ntohl(namelen);
    buf += 4;
    KFS_ASSERT(strlen(buf) == namelen);
    ret = filler(fusebuf, buf, &stbuf, offset);
    buf += namelen;
    /* Entry terminator. */
    KFS_ASSERT(*buf == '\0'); // Implied by previous strlen() assertion
    if (ret != 0) {
        KFS_RETURN(0);
    }
    /* Total size of the processed serialised entry (inc. terminator). */
    ret = buf - resbuf + 1;

    KFS_RETURN(ret);
}

static int
kenny_readdir(const char *path, void *fusebuf, fuse_fill_dir_t filler, off_t
        off, struct fuse_file_info *ffi)
{
    (void) path;

    char *resbuf = NULL;
    char operbuf[16 + 6];
    uint64_t offset_serialised = 0;
    size_t resbufsize = 0;
    size_t i = 0;
    int ret = 0;

    KFS_ENTER();

    resbuf = KFS_MALLOC(READDIR_BUFSIZE);
    if (resbuf == NULL) {
        KFS_RETURN(-ENOMEM);
    }
    memcpy(operbuf + 6, &ffi->fh, 8);
    offset_serialised = htonll(off);
    memcpy(operbuf + 14, &offset_serialised, 8);
    ret = do_operation_wrapper(KFS_OPID_READDIR, operbuf, 16, resbuf,
            READDIR_BUFSIZE, &resbufsize);
    KFS_ASSERT(ret <= 0);
    if (ret != 0) {
        resbuf = KFS_FREE(resbuf);
        KFS_RETURN(ret);
    }
    /* Simulate succesful extract_dirent() return value. */
    ret = 1;
    i = 0;
    while (i != resbufsize && ret != 0) {
        ret = extract_dirent(resbuf + i, fusebuf, filler);
        i += ret;
        KFS_ASSERT(i <= resbufsize);
    }
    resbuf = KFS_FREE(resbuf);

    KFS_RETURN(0);
}

static int
kenny_releasedir(const char *path, struct fuse_file_info *ffi)
{
    (void) path;

    char operbuf[8 + 6];
    int ret = 0;

    KFS_ENTER();

    /* The file handle. */
    memcpy(operbuf + 6, &ffi->fh, 8);
    ret = do_operation_wrapper(KFS_OPID_RELEASEDIR, operbuf, 8, NULL, 0, NULL);

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
    .write = kenny_write,
    .statfs = NULL,
    .flush = NULL,
    .release = kenny_release,
    .opendir = kenny_opendir,
    .readdir = kenny_readdir,
    .releasedir = kenny_releasedir,
};

/**
 * Main loop of the thread that handles the connection with the server. The
 * putop_mutex, a mutex that must be acquired by client threads before they can
 * put a new operation in the queue, is held by this thread all the while the
 * connection with the server is taking place. This may seem superfluous but
 * that thread will have to wait for this current operation to finish, anyway,
 * so better do it like this to reduce the chance of rare race-conditions.
 */
static void *
handlerthread_main(void *arg)
{
    (void) arg;

    int ret = 0;
    struct serialised_operation *newop = NULL;

    KFS_ENTER();

    ret = pthread_mutex_lock(&putop_mutex); KFS_ASSERT(ret == 0);
    /*
     * Wait for operation flag to be set. The conditional prevents waiting for a
     * signal that was already sent by a thread that is going super-faster than
     * this one. That is only an issue the first time: after that, the mutex is
     * never released outside cond_wait(). Also, there is no spurious wakup
     * detection, simply because there is only ever one thread (this one)
     * waiting for this condition.
     */
    if (putop == NULL) {
        ret = pthread_cond_wait(&putop_cond, &putop_mutex); KFS_ASSERT(ret ==
                0);
    }
    for (;;) {
        KFS_DEBUG("Handling new operation at %p.", (void *) putop);
        KFS_ASSERT(putop != NULL);
        newop = putop;
        putop = NULL;
        newop->clientret = do_operation(newop);
        /* Tell whoever is waiting that this operation is done. */
        ret = pthread_mutex_lock(&newop->done_mutex); KFS_ASSERT(ret == 0);
        ret = pthread_cond_signal(&newop->done_cond); KFS_ASSERT(ret == 0);
        ret = pthread_mutex_unlock(&newop->done_mutex); KFS_ASSERT(ret == 0);
        /* Wait for a new operation. */
        ret = pthread_cond_wait(&putop_cond, &putop_mutex); KFS_ASSERT(ret ==
                0);
    }

    KFS_RETURN(NULL);
}

/**
 * Initialise the handlers for the TCP brick client. This starts up the thread
 * that handles the connection with the server. Returns 0 on success, -1 on
 * failure (in which case this module can not be used).
 */
int
init_handlers(void)
{
    int ret = 0;

    KFS_ENTER();

    ret = pthread_create(&connection_thread, NULL, handlerthread_main, NULL);
    if (ret != 0) {
        KFS_ERROR("pthread_create: %s", strerror(ret));
        KFS_RETURN(-1);
    }

    KFS_RETURN(0);
}

const struct fuse_operations *
get_handlers(void)
{
    KFS_ENTER();

    KFS_ASSERT(sizeof(uint16_t) == 2 && sizeof(uint32_t) == 4);

    KFS_RETURN(&handlers);
}
