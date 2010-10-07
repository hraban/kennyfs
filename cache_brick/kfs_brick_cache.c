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
    struct kfs_subvolume * const subv = co->priv;
    struct kfs_subvolume * const cache = subv + 1;
    int ret = 0;

    KFS_ENTER();

    /* Check if data is already cached. */
    co->priv = cache->private_data;
    ret = cache->oper->getxattr(co, path, KFS_XNAME("stat"), charbuf,
            buflen);
    if (ret == buflen) {
        /* Success: the file metadata is cached. */
        memcpy(intbuf, charbuf, buflen);
        stbuf = unserialise_stat(stbuf, intbuf);
        KFS_RETURN(0);
    }
    /* There is no cached data of expected size. */
    co->priv = subv->private_data;
    ret = subv->oper->getattr(co, path, stbuf);
    if (ret != 0) {
        KFS_RETURN(ret);
    }
    /* But the file exists! Cache the metadata. */
    serialise_stat(intbuf, stbuf);
    memcpy(charbuf, intbuf, sizeof(intbuf));
    co->priv = cache->private_data;
    ret = cache->oper->setxattr(co, path, KFS_XNAME("stat"), charbuf,
            buflen, 0);
    switch (ret) {
    case 0:
        break;
    case -ENOTSUP:
        /* TODO: Disable all xattr operations from now on. */
        KFS_INFO("Caching enabled but extended attributes not supported.");
        break;
    case -ENOENT:
        /* The file does not exist. Create it and wait for next getattr call. */
        co->priv = cache->private_data;
        ret = cache->oper->mknod(co, path, S_IRUSR | S_IWUSR, 0);
        if (ret == 0) {
            break;
        }
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
    struct kfs_subvolume * const subv = co->priv;
    struct kfs_subvolume * const cache = subv + 1;
    int ret = 0;

    KFS_ENTER();

    /* Check cache. */
    co->priv = cache->private_data;
    ret = cache->oper->readlink(co, path, buf, size);
    switch (ret) {
    case -EINVAL:
        /* The cache has this file but it is not a symlink. Delete it. */
        co->priv = cache->private_data;
        cache->oper->unlink(co, path);
        break;
    case 0:
        KFS_RETURN(ret);
        break;
    default:
        break;
    }
    co->priv = subv->private_data;
    ret = subv->oper->readlink(co, path, buf, size);
    /**
     * Do not cache incomplete results. Thanks to the API the only way to be
     * sure that the result was not truncated is check whether the buffer was
     * filled entirely; O(n).
     */
    if (ret != 0 || strlen(buf) == size - 1) {
        KFS_RETURN(ret);
    }
    /* Cache. */
    co->priv = cache->private_data;
    ret = cache->oper->symlink(co, buf, path);
    if (ret != 0) {
        KFS_INFO("Error while caching symlink: %s.", strerror(-ret));
    }

    /* Ignore the return value of the cache. */
    KFS_RETURN(0);
}

static int
cache_mknod(const kfs_context_t co, const char *path, mode_t mode, dev_t dev)
{
    struct kfs_subvolume * const subv = co->priv;
    struct kfs_subvolume * const cache = subv + 1;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->mknod(co, path, mode, dev);
    if (ret != 0) {
        KFS_RETURN(ret);
    }
    co->priv = cache->private_data;
    ret = cache->oper->mknod(co, path, mode, dev);
    if (ret != 0) {
        KFS_INFO("Error while caching new node: %s.", strerror(-ret));
    }

    /* Ignore the return value of the cache. */
    KFS_RETURN(0);
}

static int
cache_truncate(const kfs_context_t co, const char *path, off_t offset)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->truncate(co, path, offset);

    KFS_RETURN(ret);
}

static int
cache_open(const kfs_context_t co, const char *path, struct fuse_file_info *fi)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->open(co, path, fi);

    KFS_RETURN(ret);
}

static int
cache_unlink(const kfs_context_t co, const char *path)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->unlink(co, path);

    KFS_RETURN(ret);
}

static int
cache_rmdir(const kfs_context_t co, const char *path)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->rmdir(co, path);

    KFS_RETURN(ret);
}

/**
 * No translation takes place for the path1 argument.
 */
static int
cache_symlink(const kfs_context_t co, const char *path1, const char *path2)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->symlink(co, path1, path2);

    KFS_RETURN(ret);
}

static int
cache_rename(const kfs_context_t co, const char *from, const char *to)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->rename(co, from, to);

    KFS_RETURN(ret);
}

static int
cache_link(const kfs_context_t co, const char *from, const char *to)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->link(co, from, to);

    KFS_RETURN(ret);
}

static int
cache_chmod(const kfs_context_t co, const char *path, mode_t mode)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->chmod(co, path, mode);

    KFS_RETURN(ret);
}

static int
cache_chown(const kfs_context_t co, const char *path, uid_t uid, gid_t gid)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->chown(co, path, uid, gid);

    KFS_RETURN(ret);
}

static int
cache_read(const kfs_context_t co, const char *path, char *buf, size_t size,
        off_t offset, struct fuse_file_info *fi)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->read(co, path, buf, size, offset, fi);

    KFS_RETURN(ret);
}

static int
cache_write(const kfs_context_t co, const char *path, const char *buf, size_t
        size, off_t offset, struct fuse_file_info *fi)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->write(co, path, buf, size, offset, fi);

    KFS_RETURN(ret);
}

static int
cache_statfs(const kfs_context_t co, const char *path, struct statvfs *stbuf)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->statfs(co, path, stbuf);

    KFS_RETURN(ret);
}

static int
cache_flush(const kfs_context_t co, const char *path, struct fuse_file_info
        *fi)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->flush(co, path, fi);

    KFS_RETURN(ret);
}

static int
cache_release(const kfs_context_t co, const char *path, struct fuse_file_info
        *fi)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->release(co, path, fi);

    KFS_RETURN(ret);
}

static int
cache_fsync(const kfs_context_t co, const char *path, int isdatasync, struct
        fuse_file_info *fi)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->fsync(co, path, isdatasync, fi);

    KFS_RETURN(ret);
}


/*
 * Extended attributes.
 */

static int
cache_setxattr(const kfs_context_t co, const char *path, const char *name,
        const char *value, size_t size, int flags)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->setxattr(co, path, name, value, size, flags);

    KFS_RETURN(ret);
}

static int
cache_getxattr(const kfs_context_t co, const char *path, const char *name, char
        *value, size_t size)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->getxattr(co, path, name, value, size);

    KFS_RETURN(ret);
}

static int
cache_listxattr(const kfs_context_t co, const char *path, char *list, size_t
        size)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->listxattr(co, path, list, size);

    KFS_RETURN(ret);
}

static int
cache_removexattr(const kfs_context_t co, const char *path, const char *name)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->removexattr(co, path, name);

    KFS_RETURN(ret);
}

/*
 * Directories.
 */

static int
cache_mkdir(const kfs_context_t co, const char *path, mode_t mode)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->mkdir(co, path, mode);

    KFS_RETURN(ret);
}

static int
cache_opendir(const kfs_context_t co, const char *path, struct fuse_file_info
        *fi)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->opendir(co, path, fi);

    KFS_RETURN(ret);
}

/**
 * List directory contents.
 */
static int
cache_readdir(const kfs_context_t co, const char *path, void *buf,
        fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->readdir(co, path, buf, filler, offset, fi);

    KFS_RETURN(ret);
}

static int
cache_releasedir(const kfs_context_t co, const char *path, struct fuse_file_info *fi)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->releasedir(co, path, fi);

    KFS_RETURN(ret);
}

static int
cache_fsyncdir(const kfs_context_t co, const char *path, int isdatasync, struct
        fuse_file_info *fi)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->fsyncdir(co, path, isdatasync, fi);

    KFS_RETURN(ret);
}

static int
cache_access(const kfs_context_t co, const char *path, int mask)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->access(co, path, mask);

    KFS_RETURN(ret);
}

static int
cache_create(const kfs_context_t co, const char *path, mode_t mode, struct
        fuse_file_info *fi)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->create(co, path, mode, fi);

    KFS_RETURN(ret);
}

static int
cache_ftruncate(const kfs_context_t co, const char *path, off_t size, struct
        fuse_file_info *fi)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->ftruncate(co, path, size, fi);
    
    KFS_RETURN(ret);
}

static int
cache_fgetattr(const kfs_context_t co, const char *path, struct stat *stbuf,
        struct fuse_file_info *fi)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->fgetattr(co, path, stbuf, fi);

    KFS_RETURN(ret);
}

static int
cache_lock(const kfs_context_t co, const char *path, struct fuse_file_info *fi,
        int cmd, struct flock *lock)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->lock(co, path, fi, cmd, lock);

    KFS_RETURN(ret);
}

static int
cache_utimens(const kfs_context_t co, const char *path, const struct timespec
        tvnano[2])
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->utimens(co, path, tvnano);

    KFS_RETURN(ret);
}

static int
cache_bmap(const kfs_context_t co, const char *path, size_t blocksize, uint64_t
        *idx)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->bmap(co, path, blocksize, idx);

    KFS_RETURN(ret);
}

#if FUSE_VERSION >= 28
static int
cache_ioctl(const kfs_context_t co, const char *path, int cmd, void *arg,
        struct fuse_file_info *fi, uint_t flags, void *data)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->ioctl(co, path, cmd, arg, fi, flags, data);

    KFS_RETURN(ret);
}
#endif

#if FUSE_VERSION >= 28
static int
cache_poll(const kfs_context_t co, const char *path, struct fuse_file_info *fi,
        struct fuse_pollhandle *ph, uint_t *reventsp)
{
    struct kfs_subvolume * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    co->priv = subv->private_data;
    ret = subv->oper->poll(co, path, fi, ph, reventsp);

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
        const struct kfs_subvolume subvolumes[])
{
    (void) conffile;
    (void) section;

    struct kfs_subvolume *subvols_cpy = NULL;

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
