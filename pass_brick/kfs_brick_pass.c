/**
 * KennyFS brick that passes through all FUSE operations verbatim. Useful for
 * testing and debugging. Also serves as a template for new bricks.
 */

#define FUSE_USE_VERSION 29

#include "pass_brick/kfs_brick_pass.h"

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

static int
pass_getattr(const kfs_context_t co, const char *path, struct stat *stbuf)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, getattr, co, path, stbuf);

    KFS_RETURN(ret);
}

static int
pass_readlink(const kfs_context_t co, const char *path, char *buf, size_t
        size)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, readlink, co, path, buf, size);

    KFS_RETURN(ret);
}

static int
pass_mknod(const kfs_context_t co, const char *path, mode_t mode, dev_t dev)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, mknod, co, path, mode, dev);

    KFS_RETURN(ret);
}

static int
pass_truncate(const kfs_context_t co, const char *path, off_t offset)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, truncate, co, path, offset);

    KFS_RETURN(ret);
}

static int
pass_open(const kfs_context_t co, const char *path, struct fuse_file_info *fi)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, open, co, path, fi);

    KFS_RETURN(ret);
}

static int
pass_unlink(const kfs_context_t co, const char *path)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, unlink, co, path);

    KFS_RETURN(ret);
}

static int
pass_rmdir(const kfs_context_t co, const char *path)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, rmdir, co, path);

    KFS_RETURN(ret);
}

/**
 * No translation takes place for the path1 argument.
 */
static int
pass_symlink(const kfs_context_t co, const char *path1, const char *path2)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, symlink, co, path1, path2);

    KFS_RETURN(ret);
}

static int
pass_rename(const kfs_context_t co, const char *from, const char *to)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, rename, co, from, to);

    KFS_RETURN(ret);
}

static int
pass_link(const kfs_context_t co, const char *from, const char *to)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, link, co, from, to);

    KFS_RETURN(ret);
}

static int
pass_chmod(const kfs_context_t co, const char *path, mode_t mode)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, chmod, co, path, mode);

    KFS_RETURN(ret);
}

static int
pass_chown(const kfs_context_t co, const char *path, uid_t uid, gid_t gid)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, chown, co, path, uid, gid);

    KFS_RETURN(ret);
}

static int
pass_read(const kfs_context_t co, const char *path, char *buf, size_t size,
        off_t offset, struct fuse_file_info *fi)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, read, co, path, buf, size, offset, fi);

    KFS_RETURN(ret);
}

static int
pass_write(const kfs_context_t co, const char *path, const char *buf, size_t
        size, off_t offset, struct fuse_file_info *fi)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, write, co, path, buf, size, offset, fi);

    KFS_RETURN(ret);
}

static int
pass_statfs(const kfs_context_t co, const char *path, struct statvfs *stbuf)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, statfs, co, path, stbuf);

    KFS_RETURN(ret);
}

static int
pass_flush(const kfs_context_t co, const char *path, struct fuse_file_info
        *fi)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, flush, co, path, fi);

    KFS_RETURN(ret);
}

static int
pass_release(const kfs_context_t co, const char *path, struct fuse_file_info
        *fi)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, release, co, path, fi);

    KFS_RETURN(ret);
}

static int
pass_fsync(const kfs_context_t co, const char *path, int isdatasync, struct
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
pass_setxattr(const kfs_context_t co, const char *path, const char *name,
        const char *value, size_t size, int flags)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, setxattr, co, path, name, value, size, flags);

    KFS_RETURN(ret);
}

static int
pass_getxattr(const kfs_context_t co, const char *path, const char *name, char
        *value, size_t size)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, getxattr, co, path, name, value, size);

    KFS_RETURN(ret);
}

static int
pass_listxattr(const kfs_context_t co, const char *path, char *list, size_t
        size)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, listxattr, co, path, list, size);

    KFS_RETURN(ret);
}

static int
pass_removexattr(const kfs_context_t co, const char *path, const char *name)
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
pass_mkdir(const kfs_context_t co, const char *path, mode_t mode)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, mkdir, co, path, mode);

    KFS_RETURN(ret);
}

static int
pass_opendir(const kfs_context_t co, const char *path, struct fuse_file_info
        *fi)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, opendir, co, path, fi);

    KFS_RETURN(ret);
}

/**
 * List directory contents.
 */
static int
pass_readdir(const kfs_context_t co, const char *path, void *buf,
        fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, readdir, co, path, buf, filler, offset, fi);

    KFS_RETURN(ret);
}

static int
pass_releasedir(const kfs_context_t co, const char *path, struct fuse_file_info *fi)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, releasedir, co, path, fi);

    KFS_RETURN(ret);
}

static int
pass_fsyncdir(const kfs_context_t co, const char *path, int isdatasync, struct
        fuse_file_info *fi)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, fsyncdir, co, path, isdatasync, fi);

    KFS_RETURN(ret);
}

static int
pass_access(const kfs_context_t co, const char *path, int mask)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, access, co, path, mask);

    KFS_RETURN(ret);
}

static int
pass_create(const kfs_context_t co, const char *path, mode_t mode, struct
        fuse_file_info *fi)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, create, co, path, mode, fi);

    KFS_RETURN(ret);
}

static int
pass_ftruncate(const kfs_context_t co, const char *path, off_t size, struct
        fuse_file_info *fi)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, ftruncate, co, path, size, fi);
    
    KFS_RETURN(ret);
}

static int
pass_fgetattr(const kfs_context_t co, const char *path, struct stat *stbuf,
        struct fuse_file_info *fi)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, fgetattr, co, path, stbuf, fi);

    KFS_RETURN(ret);
}

static int
pass_lock(const kfs_context_t co, const char *path, struct fuse_file_info *fi,
        int cmd, struct flock *lock)
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, lock, co, path, fi, cmd, lock);

    KFS_RETURN(ret);
}

static int
pass_utimens(const kfs_context_t co, const char *path, const struct timespec
        tvnano[2])
{
    struct kfs_brick * const subv = co->priv;
    int ret = 0;

    KFS_ENTER();

    KFS_DO_OPER(ret = , subv, utimens, co, path, tvnano);

    KFS_RETURN(ret);
}

static int
pass_bmap(const kfs_context_t co, const char *path, size_t blocksize, uint64_t
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
pass_ioctl(const kfs_context_t co, const char *path, int cmd, void *arg,
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
pass_poll(const kfs_context_t co, const char *path, struct fuse_file_info *fi,
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
    .getattr = pass_getattr,
    .readlink = pass_readlink,
    .mknod = pass_mknod,
    .mkdir = pass_mkdir,
    .unlink = pass_unlink,
    .rmdir = pass_rmdir,
    .symlink = pass_symlink,
    .rename = pass_rename,
    .link = pass_link,
    .chmod = pass_chmod,
    .chown = pass_chown,
    .truncate = pass_truncate,
    .open = pass_open,
    .read = pass_read,
    .write = pass_write,
    .statfs = pass_statfs,
    .flush = pass_flush,
    .release = pass_release,
    .fsync = pass_fsync,
    .setxattr = pass_setxattr,
    .getxattr = pass_getxattr,
    .listxattr = pass_listxattr,
    .removexattr = pass_removexattr,
    .opendir = pass_opendir,
    .readdir = pass_readdir,
    .releasedir = pass_releasedir,
    .fsyncdir = pass_fsyncdir,
    .access = pass_access,
    .create = pass_create,
    .ftruncate = pass_ftruncate,
    .fgetattr = pass_fgetattr,
    .lock = pass_lock,
    .utimens = pass_utimens,
    .bmap = pass_bmap,
#if FUSE_VERSION >= 28
    .ioctl = pass_ioctl,
    .poll = pass_poll,
#endif
};

/**
 * Global initialization.
 */
static void *
kfs_pass_init(const char *conffile, const char *section, size_t num_subvolumes,
        const struct kfs_brick subvolumes[])
{
    (void) conffile;
    (void) section;

    struct kfs_brick *subv_copy = NULL;

    KFS_ENTER();

    KFS_ASSERT(conffile != NULL && section != NULL && subvolumes != NULL);
    if (num_subvolumes != 1) {
        KFS_ERROR("Exactly one subvolume required by brick %s.", section);
        KFS_RETURN(NULL);
    }
    subv_copy = KFS_MALLOC(sizeof(*subv_copy));
    if (subv_copy == NULL) {
        KFS_RETURN(NULL);
    }
    *subv_copy = subvolumes[0];

    KFS_RETURN(subv_copy);
}

/*
 * Get the backend interface.
 */
static const struct kfs_operations *
kfs_pass_getfuncs(void)
{
    KFS_ENTER();

    KFS_RETURN(&handlers);
}

/**
 * Global cleanup.
 */
static void
kfs_pass_halt(void *private_data)
{
    KFS_ENTER();

    private_data = KFS_FREE(private_data);

    KFS_RETURN();
}

static const struct kfs_brick_api kfs_pass_api = {
    .init = kfs_pass_init,
    .getfuncs = kfs_pass_getfuncs,
    .halt = kfs_pass_halt,
};

const struct kfs_brick_api *
kfs_brick_getapi(void)
{
    KFS_ENTER();

    KFS_RETURN(&kfs_pass_api);
}
