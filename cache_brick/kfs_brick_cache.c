/**
 * KennyFS brick that caches calls to the first subvolume by storing it in the
 * second.
 *
 * Does not do any cache expiration, i.e.: if the file is cached, that copy is
 * always considered valid.
 */

#define FUSE_USE_VERSION 29

#include "cache_brick/kfs_brick_cache.h"

#include <errno.h>
#include <fuse.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "kfs.h"
#include "kfs_api.h"
#include "kfs_misc.h"

#define LOCAL_XATTR_NS KFS_XATTR_NS ".brick.cache"

#define KFS_XNAME(suffix) (LOCAL_XATTR_NS "." suffix)

/**
 * Context needed by cache_readdir_filler() to communicate with cache_readdir().
 */
struct readdir_context {
    /** Original filler function. */
    fuse_fill_dir_t filler;
    /** Original buffer. */
    void *buf;
    /** Set to 1 if any failure occured while caching (ie do not trust cache) */
    uint_t failure;
    /** The source brick. */
    struct kfs_brick *orig_brick;
    /** The target (caching) brick. */
    struct kfs_brick *cache_brick;
    /** The context used by those bricks. */
    kfs_context_t kfs_context;
};

enum fh_type {
    FH_CACHE,
    FH_ORIG,
};

/**
 * Tuple of two file handles: one opened cached dir, one opened original dir.
 */
struct dirfh_switch {
    uint64_t fh;
    enum fh_type type;
};

/**
 * Create a node of given mode on the cache, optionally using orig to look up
 * necessary data (symlink target). Properly handles different types of nodes
 * (dir, symlink, regular, etc).
 */
static int
versatile_mknod(struct kfs_brick *orig, struct kfs_brick *cache, const
        kfs_context_t co, const char *path, mode_t mode)
{
    int ret = 0;
    char *charbuf = NULL;
    /**
     * TODO: Make this dynamic (realloc as much as necessary) to prevent one
     * length symlink target from causing havoc.
     */
    const size_t bufsize = 1000;

    KFS_ENTER();

    if (S_ISDIR(mode)) {
        KFS_DO_OPER(ret = , cache, mkdir, co, path, mode);
    } else if (S_ISLNK(mode)) {
        /* cache_readlink will cache the symlink. */
        charbuf = KFS_MALLOC(bufsize);
        if (charbuf == NULL) {
            KFS_RETURN(-1);
        }
        /**
         * Do not cache incomplete results. Thanks to the API the only way to be
         * sure that the result was not truncated is check whether the buffer
         * was filled entirely; O(n).
         */
        KFS_DO_OPER(ret = , orig, readlink, co, path, charbuf, bufsize);
        if (ret != 0 || strlen(charbuf) == bufsize - 1) {
            charbuf = KFS_FREE(charbuf);
            KFS_RETURN(-1);
        }
        /* Store symlink target. */
        KFS_DO_OPER(ret = , cache, symlink, co, charbuf, path);
        charbuf = KFS_FREE(charbuf);
    } else {
        KFS_DO_OPER(ret = , cache, mknod, co, path, mode, 0);
    }

    KFS_RETURN(ret);
}

/**
 * Caches the result in extended attributes of the cache copy.
 *
 * This is to prevent opening the can of worms that is manual setattr() on files
 * on different filesystems, if that is even possible at all.
 *
 * As per the POSIX spec (TODO: is that really in the spec? I got it from man
 * 3posix stat) (at least) the following members are cached:
 *
 * - st_mode
 * - st_ino
 * - st_dev
 * - st_uid
 * - st_gid
 * - st_atime
 * - st_ctime
 * - st_mtime
 * - st_nlink
 */
static int
cache_getattr(const kfs_context_t co, const char *path, struct stat *stbuf)
{
    uint32_t intbuf[13];
    const size_t buflen = sizeof(intbuf);
    char charbuf[buflen];
    struct kfs_brick * const subv = co->priv;
    struct kfs_brick * const cache = subv + 1;
    int ret = 0;
    mode_t mode = 0;

    KFS_ENTER();

    /* Check if data is already cached. */
    KFS_DO_OPER(ret = , cache, getxattr, co, path, KFS_XNAME("stat"), charbuf,
            buflen);
    if (ret == buflen) {
        /* Success: the file metadata is cached. */
        memcpy(intbuf, charbuf, buflen);
        stbuf = unserialise_stat(stbuf, intbuf);
        KFS_RETURN(0);
    }
    /* There is no cached data of expected size. */
    KFS_DO_OPER(ret = , subv, getattr, co, path, stbuf);
    if (ret != 0) {
        KFS_RETURN(ret);
    }
    /* But the file exists! Cache the metadata. */
    serialise_stat(intbuf, stbuf);
    memcpy(charbuf, intbuf, buflen);
    KFS_DO_OPER(ret = , cache, setxattr, co, path, KFS_XNAME("stat"), charbuf,
            buflen, 0);
    switch (ret) {
    case 0:
        break;
    case -ENOTSUP:
        /* TODO: Disable all xattr operations from now on? */
        KFS_INFO("Caching enabled but extended attributes not supported.");
        break;
    case -ENOENT:
        /* The file does not exist. Create it and wait for next getattr call. */
        mode = (stbuf->st_mode & S_IFMT) | S_IRWXU;
        ret = versatile_mknod(subv, cache, co, path, mode);
        if (ret != 0) {
            KFS_INFO("Error while caching metadata.");
        }
        break;
    default:
        KFS_INFO("Error while caching metadata: %s.", strerror(-ret));
        break;
    }

    /* Ignore return value of cache. */
    KFS_RETURN(0);
}

static int
cache_readlink(const kfs_context_t co, const char *path, char *buf, size_t
        size)
{
    struct kfs_brick * const subv = co->priv;
    struct kfs_brick * const cache = subv + 1;
    int ret = 0;

    KFS_ENTER();

    /* Check cache. */
    KFS_DO_OPER(ret = , cache, readlink, co, path, buf, size);
    switch (ret) {
    case -EINVAL:
        /* The cache has this file but it is not a symlink. Delete it. */
        KFS_DO_OPER(/**/, cache, unlink, co, path);
        break;
    case 0:
        KFS_RETURN(ret);
        break;
    default:
        break;
    }
    KFS_DO_OPER(ret = , subv, readlink, co, path, buf, size);
    /**
     * Do not cache incomplete results. Thanks to the API the only way to be
     * sure that the result was not truncated is check whether the buffer was
     * filled entirely; O(n).
     */
    if (ret != 0 || strlen(buf) == size - 1) {
        KFS_RETURN(ret);
    }
    /* Cache. */
    KFS_DO_OPER(ret = , cache, symlink, co, buf, path);
    if (ret != 0) {
        KFS_INFO("Error while caching symlink: %s.", strerror(-ret));
    }

    /* Ignore the return value of the cache. */
    KFS_RETURN(0);
}

static int
cache_mknod(const kfs_context_t co, const char *path, mode_t mode, dev_t dev)
{
    struct kfs_brick * const subv = co->priv;
    struct kfs_brick * const cache = subv + 1;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, mknod, co, path, mode, dev);
    if (ret != 0) {
        KFS_RETURN(ret);
    }
    KFS_DO_OPER(ret = , cache, mknod, co, path, PERM0600, dev);
    if (ret != 0) {
        KFS_INFO("Error while caching new node: %s.", strerror(-ret));
    }

    /* Ignore the return value of the cache. */
    KFS_RETURN(0);
}

static int
cache_truncate(const kfs_context_t co, const char *path, off_t offset)
{
    struct kfs_brick * const subv = co->priv;
    struct kfs_brick * const cache = subv + 1;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, truncate, co, path, offset);
    if (ret != 0) {
        KFS_RETURN(ret);
    }
    KFS_DO_OPER(ret = , cache, truncate, co, path, offset);
    if (ret != 0 && ret != -ENOENT) {
        KFS_INFO("Error while truncating cached file: %s.", strerror(-ret));
        /* Only one recourse to keep cache coherent: remove the cached file. */
        KFS_DO_OPER(ret = , cache, unlink, co, path);
        if (ret != 0) {
            KFS_ERROR("Corrupt cache: file \"%s\" could not be removed: %s",
                    path, strerror(-ret));
        }
    }

    /* Ignore the return value of the cache. */
    KFS_RETURN(0);
}

static int
cache_open(const kfs_context_t co, const char *path, struct fuse_file_info *fi)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, open, co, path, fi);

    KFS_RETURN(ret);
}

static int
cache_unlink(const kfs_context_t co, const char *path)
{
    struct kfs_brick * const subv = co->priv;
    struct kfs_brick * const cache = subv + 1;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, unlink, co, path);
    if (ret != 0) {
        KFS_RETURN(ret);
    }
    KFS_DO_OPER(ret = , cache, unlink, co, path);
    if (ret != 0 && ret != -ENOENT) {
        KFS_ERROR("Corrupt cache: file \"%s\" could not be removed: %s", path,
                strerror(-ret));
    }

    KFS_RETURN(0);
}

static int
cache_rmdir(const kfs_context_t co, const char *path)
{
    struct kfs_brick * const subv = co->priv;
    struct kfs_brick * const cache = subv + 1;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, rmdir, co, path);
    if (ret != 0) {
        KFS_RETURN(ret);
    }
    KFS_DO_OPER(ret = , cache, rmdir, co, path);
    if (ret != 0) {
        KFS_ERROR("Corrupt cache: directory \"%s\" could not be removed: %s",
                path, strerror(-ret));
    }

    KFS_RETURN(0);
}

static int
cache_symlink(const kfs_context_t co, const char *path1, const char *path2)
{
    struct kfs_brick * const subv = co->priv;
    struct kfs_brick * const cache = subv + 1;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, symlink, co, path1, path2);
    if (ret != 0) {
        KFS_RETURN(ret);
    }
    KFS_DO_OPER(ret = , cache, symlink, co, path1, path2);
    if (ret != 0) {
        KFS_INFO("Error while caching symlink: %s.", strerror(-ret));
    }

    KFS_RETURN(ret);
}

static int
cache_rename(const kfs_context_t co, const char *from, const char *to)
{
    struct kfs_brick * const subv = co->priv;
    struct kfs_brick * const cache = subv + 1;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, rename, co, from, to);
    if (ret != 0) {
        KFS_RETURN(ret);
    }
    KFS_DO_OPER(ret = , cache, rename, co, from, to);
    if (ret != 0 && ret != -ENOENT) {
        KFS_INFO("Error while caching file rename: %s.", strerror(-ret));
    }

    KFS_RETURN(ret);
}

static int
cache_link(const kfs_context_t co, const char *from, const char *to)
{
    struct kfs_brick * const subv = co->priv;
    struct kfs_brick * const cache = subv + 1;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, link, co, from, to);
    if (ret != 0) {
        KFS_RETURN(ret);
    }
    KFS_DO_OPER(ret = , cache, link, co, from, to);
    if (ret != 0 && ret != -ENOENT) {
        KFS_INFO("Error while caching hardlink: %s.", strerror(-ret));
    }

    KFS_RETURN(ret);
}

static int
cache_chmod(const kfs_context_t co, const char *path, mode_t mode)
{
    struct kfs_brick * const subv = co->priv;
    struct kfs_brick * const cache = subv + 1;
    uint32_t intbuf[13];
    const size_t buflen = sizeof(intbuf);
    char charbuf[buflen];
    struct stat _stbuf;
    struct stat * const stbuf = &_stbuf;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, chmod, co, path, mode);
    if (ret != 0) {
        KFS_RETURN(ret);
    }
    /* Get the attributes of this file (reuse this module's getattr()). */
    co->priv = subv;
    ret = cache_getattr(co, path, stbuf);
    if (ret != 0) {
        /* If getattr() fails this can not be cached (but chmod() succeeded). */
        KFS_RETURN(0);
    }
    /* Update those attributes. */
    stbuf->st_mode = mode;
    serialise_stat(intbuf, stbuf);
    memcpy(charbuf, intbuf, buflen);
    KFS_DO_OPER(ret = , cache, setxattr, co, path, KFS_XNAME("stat"), charbuf,
            buflen, 0);
    if (ret != 0) {
        KFS_INFO("Error while caching metadata: %s.", strerror(-ret));
    }

    KFS_RETURN(0);
}

static int
cache_chown(const kfs_context_t co, const char *path, uid_t uid, gid_t gid)
{
    struct kfs_brick * const subv = co->priv;
    struct kfs_brick * const cache = subv + 1;
    uint32_t intbuf[13];
    const size_t buflen = sizeof(intbuf);
    char charbuf[buflen];
    struct stat _stbuf;
    struct stat * const stbuf = &_stbuf;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, chown, co, path, uid, gid);
    if (ret != 0) {
        KFS_RETURN(ret);
    }
    /* Get the attributes of this file (reuse this module's getattr()). */
    co->priv = subv;
    ret = cache_getattr(co, path, stbuf);
    if (ret != 0) {
        /* If getattr() fails this can not be cached (still return 0). */
        KFS_RETURN(0);
    }
    /* Update those attributes. */
    stbuf->st_uid = uid;
    stbuf->st_gid = gid;
    serialise_stat(intbuf, stbuf);
    memcpy(charbuf, intbuf, buflen);
    KFS_DO_OPER(ret = , cache, setxattr, co, path, KFS_XNAME("stat"), charbuf,
            buflen, 0);
    if (ret != 0) {
        KFS_INFO("Error while caching metadata: %s.", strerror(-ret));
    }

    KFS_RETURN(ret);
}

static int
cache_read(const kfs_context_t co, const char *path, char *buf, size_t size,
        off_t offset, struct fuse_file_info *fi)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, read, co, path, buf, size, offset, fi);

    KFS_RETURN(ret);
}

static int
cache_write(const kfs_context_t co, const char *path, const char *buf, size_t
        size, off_t offset, struct fuse_file_info *fi)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, write, co, path, buf, size, offset, fi);

    KFS_RETURN(ret);
}

static int
cache_statfs(const kfs_context_t co, const char *path, struct statvfs *stbuf)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, statfs, co, path, stbuf);

    KFS_RETURN(ret);
}

static int
cache_flush(const kfs_context_t co, const char *path, struct fuse_file_info
        *fi)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, flush, co, path, fi);

    KFS_RETURN(ret);
}

static int
cache_release(const kfs_context_t co, const char *path, struct fuse_file_info
        *fi)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, release, co, path, fi);

    KFS_RETURN(ret);
}

static int
cache_fsync(const kfs_context_t co, const char *path, int isdatasync, struct
        fuse_file_info *fi)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, fsync, co, path, isdatasync, fi);

    KFS_RETURN(ret);
}


/*
 * Extended attributes.
 */

static int
cache_setxattr(const kfs_context_t co, const char *path, const char *name,
        const char *value, size_t size, int flags)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, setxattr, co, path, name, value, size, flags);

    KFS_RETURN(ret);
}

static int
cache_getxattr(const kfs_context_t co, const char *path, const char *name, char
        *value, size_t size)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, getxattr, co, path, name, value, size);

    KFS_RETURN(ret);
}

static int
cache_listxattr(const kfs_context_t co, const char *path, char *list, size_t
        size)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, listxattr, co, path, list, size);

    KFS_RETURN(ret);
}

static int
cache_removexattr(const kfs_context_t co, const char *path, const char *name)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, removexattr, co, path, name);

    KFS_RETURN(ret);
}

/*
 * Directories.
 */

static int
cache_mkdir(const kfs_context_t co, const char *path, mode_t mode)
{
    struct kfs_brick * const subv = co->priv;
    struct kfs_brick * const cache = subv + 1;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, mkdir, co, path, mode);
    if (ret != 0) {
        KFS_RETURN(ret);
    }
    KFS_DO_OPER(ret = , cache, mkdir, co, path, PERM0700);
    if (ret != 0) {
        KFS_INFO("Error while caching new dir: %s.", strerror(-ret));
    }

    KFS_RETURN(0);
}

static int
cache_opendir(const kfs_context_t co, const char *path, struct fuse_file_info
        *fi)
{
    struct kfs_brick * const subv = co->priv;
    struct kfs_brick * const cache = subv + 1;
    struct dirfh_switch *fh = NULL;
    char c = '\0';
    int ret = 0;

    KFS_ENTER();

    fh = KFS_MALLOC(sizeof(*fh));
    if (fh == NULL) {
        KFS_RETURN(-ENOMEM);
    }
    KFS_DO_OPER(ret = , cache, getxattr, co, path, KFS_XNAME("readdir"), &c, 1);
    if (ret == 0 && c == '\0') {
        /** The whole dir is already cached, no need to open the source. */
        KFS_DO_OPER(ret = , cache, opendir, co, path, fi);
        if (ret == 0) {
            fh->type = FH_CACHE;
            fh->fh = fi->fh;
        } else {
            KFS_INFO("Error while opening cached dir: %s", strerror(-ret));
        }
    }
    if (ret != 0) {
        KFS_DO_OPER(ret = , subv, opendir, co, path, fi);
        if (ret == 0) {
            fh->type = FH_ORIG;
            fh->fh = fi->fh;
        }
    }
    memcpy(&fi->fh, &fh, sizeof(fh));

    KFS_RETURN(ret);
}

/**
 * Wrapper around the caller's readdir callback (the "filler" function). It
 * passes all operations through and sets a flag if the filler indicates that
 * its buffer is full, so that the readdir() operation handler knows about it.
 */
static int
cache_readdir_filler(void *buf, const char *name, const struct stat *stbuf,
        off_t offset)
{
    struct readdir_context * const rd_co = buf;
    int ret = 0;

    KFS_ENTER();

    ret = rd_co->filler(rd_co->buf, name, stbuf, offset);
    if (ret == 0) {
        ret = versatile_mknod(rd_co->orig_brick, rd_co->cache_brick,
                rd_co->kfs_context, name, (stbuf->st_mode & S_IFMT) | S_IRWXU);
        if (ret == -1) {
            rd_co->failure = 1;
        }
    } else {
        /* Buffer is full. */
        rd_co->failure = 1;
    }

    KFS_RETURN(ret);
}

/**
 * List directory contents. If this directory has the extended attribute
 * "readdir" (in this namespace), with no contents, the cached directory is read
 * instead. Otherwise (if there is no such attribute or if it has any contents),
 * the source directory is read, the cached directory is updated and the
 * "readdir" attribute is set to an empty string.
 * 
 * TODO: There is still room for a subtle bug: if this function is called
 * "asynchronously" (i.e.: multiple times, but not with incrementing offsets),
 * it might reach the end of the directory before having all the contents. It
 * will not recognise this situation and (for this and every following call)
 * consider the directory entries properly cached, meaning some entries will not
 * be visible to the caller.
 */
static int
cache_readdir(const kfs_context_t co, const char *path, void *buf,
        fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    struct kfs_brick * const subv = co->priv;
    struct kfs_brick * const cache = subv + 1;
    struct readdir_context rd_context;
    struct dirfh_switch *fh = NULL;
    int ret = 0;

    KFS_ENTER();

    memcpy(&fh, &fi->fh, sizeof(fh));
    fi->fh = fh->fh;
    KFS_ASSERT(fh != NULL);
    if (fh->type == FH_CACHE) {
        /* Read from cache. */
        KFS_DO_OPER(ret = , cache, readdir, co, path, buf, filler, offset, fi);
        KFS_RETURN(ret);
    }
    KFS_ASSERT(fh->type == FH_ORIG);
    rd_context.filler = filler;
    rd_context.buf = buf;
    rd_context.failure = 0;
    rd_context.orig_brick = subv;
    rd_context.cache_brick = cache;
    rd_context.kfs_context = co;
    KFS_DO_OPER(ret = , subv, readdir, co, path, &rd_context,
            cache_readdir_filler, offset, fi);
    if (ret == 0 && rd_context.failure == 0) {
        /* All entries were properly processed. */
        KFS_DO_OPER(/**/, cache, setxattr, co, path, KFS_XNAME("readdir"), "",
                0, 0);
    }

    KFS_RETURN(ret);
}

static int
cache_releasedir(const kfs_context_t co, const char *path, struct fuse_file_info *fi)
{
    struct kfs_brick * const subv = co->priv;
    struct kfs_brick * const cache = subv + 1;
    struct dirfh_switch *fh = NULL;
    int ret = 0;

    KFS_ENTER();

    memcpy(&fh, &fi->fh, sizeof(fh));
    fi->fh = fh->fh;
    KFS_ASSERT(fh != NULL);
    if (fh->type == FH_CACHE) {
        KFS_DO_OPER(ret = , cache, releasedir, co, path, fi);
    } else {
        KFS_ASSERT(fh->type == FH_ORIG);
        KFS_DO_OPER(ret = , subv, releasedir, co, path, fi);
    }
    fh = KFS_FREE(fh);

    KFS_RETURN(ret);
}

static int
cache_fsyncdir(const kfs_context_t co, const char *path, int isdatasync, struct
        fuse_file_info *fi)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, fsyncdir, co, path, isdatasync, fi);

    KFS_RETURN(ret);
}

static int
cache_access(const kfs_context_t co, const char *path, int mask)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, access, co, path, mask);

    KFS_RETURN(ret);
}

static int
cache_create(const kfs_context_t co, const char *path, mode_t mode, struct
        fuse_file_info *fi)
{
    struct kfs_brick * const subv = co->priv;
    struct kfs_brick * const cache = subv + 1;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, create, co, path, mode, fi);
    if (ret != 0) {
        KFS_RETURN(ret);
    }
    /* create = (mknod, open), but open is not used on cache (yet). */
    KFS_DO_OPER(ret = , cache, mknod, co, path, mode, 0);
    if (ret != 0) {
        KFS_INFO("Error while caching new file: %s.", strerror(-ret));
    }

    KFS_RETURN(0);
}

static int
cache_ftruncate(const kfs_context_t co, const char *path, off_t size, struct
        fuse_file_info *fi)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, ftruncate, co, path, size, fi);
    
    KFS_RETURN(ret);
}

static int
cache_fgetattr(const kfs_context_t co, const char *path, struct stat *stbuf,
        struct fuse_file_info *fi)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, fgetattr, co, path, stbuf, fi);

    KFS_RETURN(ret);
}

static int
cache_lock(const kfs_context_t co, const char *path, struct fuse_file_info *fi,
        int cmd, struct flock *lock)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, lock, co, path, fi, cmd, lock);

    KFS_RETURN(ret);
}

static int
cache_utimens(const kfs_context_t co, const char *path, const struct timespec
        tvnano[2])
{
    struct kfs_brick * const subv = co->priv;
    struct kfs_brick * const cache = subv + 1;
    uint32_t intbuf[13];
    const size_t buflen = sizeof(intbuf);
    char charbuf[buflen];
    struct stat _stbuf;
    struct stat * const stbuf = &_stbuf;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, utimens, co, path, tvnano);
    if (ret != 0) {
        KFS_RETURN(ret);
    }
    /* Get the attributes of this file (reuse this module's getattr()). */
    co->priv = subv;
    ret = cache_getattr(co, path, stbuf);
    if (ret != 0) {
        /* If getattr() fails this can not be cached (but utimens succeeded). */
        KFS_RETURN(0);
    }
    /* Update those attributes. */
    stbuf->st_atime = tvnano[0].tv_sec;
    stbuf->st_mtime = tvnano[1].tv_sec;
    serialise_stat(intbuf, stbuf);
    memcpy(charbuf, intbuf, buflen);
    KFS_DO_OPER(ret = , cache, setxattr, co, path, KFS_XNAME("stat"), charbuf,
            buflen, 0);
    if (ret != 0) {
        KFS_INFO("Error while caching metadata: %s.", strerror(-ret));
    }

    KFS_RETURN(0);
}

static int
cache_bmap(const kfs_context_t co, const char *path, size_t blocksize, uint64_t
        *idx)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, bmap, co, path, blocksize, idx);

    KFS_RETURN(ret);
}

#if FUSE_VERSION >= 28
static int
cache_ioctl(const kfs_context_t co, const char *path, int cmd, void *arg,
        struct fuse_file_info *fi, uint_t flags, void *data)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, ioctl, co, path, cmd, arg, fi, flags, data);

    KFS_RETURN(ret);
}
#endif

#if FUSE_VERSION >= 28
static int
cache_poll(const kfs_context_t co, const char *path, struct fuse_file_info *fi,
        struct fuse_pollhandle *ph, uint_t *reventsp)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, poll, co, path, fi, ph, reventsp);

    KFS_RETURN(ret);
}
#endif

static const struct kfs_operations handlers = {
    .getattr = cache_getattr,
    .readlink = cache_readlink,
    .mknod = cache_mknod,
    .mkdir = cache_mkdir,
    .unlink = cache_unlink,
    .rmdir = cache_rmdir,
    .symlink = cache_symlink,
    .rename = cache_rename,
    .link = cache_link,
    .chmod = cache_chmod,
    .chown = cache_chown,
    .truncate = cache_truncate,
    .open = cache_open,
    .read = cache_read,
    .write = cache_write,
    .statfs = cache_statfs,
    .flush = cache_flush,
    .release = cache_release,
    .fsync = cache_fsync,
    .setxattr = cache_setxattr,
    .getxattr = cache_getxattr,
    .listxattr = cache_listxattr,
    .removexattr = cache_removexattr,
    .opendir = cache_opendir,
    .readdir = cache_readdir,
    .releasedir = cache_releasedir,
    .fsyncdir = cache_fsyncdir,
    .access = cache_access,
    .create = cache_create,
    .ftruncate = cache_ftruncate,
    .fgetattr = cache_fgetattr,
    .lock = cache_lock,
    .utimens = cache_utimens,
    .bmap = cache_bmap,
#if FUSE_VERSION >= 28
    .ioctl = cache_ioctl,
    .poll = cache_poll,
#endif
};

/**
 * Global initialization. Requires exactly two subvolumes: the first one is the
 * origin, the second one is the cache.
 */
static void *
kfs_cache_init(const char *conffile, const char *section, size_t num_subvolumes,
        const struct kfs_brick subvolumes[])
{
    (void) conffile;
    (void) section;

    struct kfs_brick *subvols_cpy = NULL;

    KFS_ENTER();

    KFS_ASSERT(conffile != NULL && section != NULL && subvolumes != NULL);
    if (num_subvolumes != 2) {
        KFS_ERROR("Exactly two subvolumes required by brick %s.", section);
        KFS_RETURN(NULL);
    }
    subvols_cpy = KFS_MALLOC(sizeof(*subvols_cpy) * 2);
    if (subvols_cpy == NULL) {
        KFS_RETURN(NULL);
    }
    subvols_cpy = memcpy(subvols_cpy, subvolumes, sizeof(*subvols_cpy) * 2);

    KFS_RETURN(subvols_cpy);
}

/*
 * Get the backend interface.
 */
static const struct kfs_operations *
kfs_cache_getfuncs(void)
{
    KFS_ENTER();

    KFS_RETURN(&handlers);
}

/**
 * Global cleanup.
 */
static void
kfs_cache_halt(void *private_data)
{
    KFS_ENTER();

    private_data = KFS_FREE(private_data);

    KFS_RETURN();
}

static const struct kfs_brick_api kfs_cache_api = {
    .init = kfs_cache_init,
    .getfuncs = kfs_cache_getfuncs,
    .halt = kfs_cache_halt,
};

const struct kfs_brick_api *
kfs_brick_getapi(void)
{
    KFS_ENTER();

    KFS_RETURN(&kfs_cache_api);
}
