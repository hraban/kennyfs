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
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->getattr == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->getattr(co, path, stbuf);

    KFS_RETURN(ret);
}

static int
pass_readlink(const kfs_context_t co, const char *path, char *buf, size_t
        size)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    ssize_t ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->readlink == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->readlink(co, path, buf, size);

    KFS_RETURN(ret);
}

static int
pass_mknod(const kfs_context_t co, const char *path, mode_t mode, dev_t dev)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->mknod == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->mknod(co, path, mode, dev);

    KFS_RETURN(ret);
}

static int
pass_truncate(const kfs_context_t co, const char *path, off_t offset)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->truncate == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->truncate(co, path, offset);

    KFS_RETURN(ret);
}

static int
pass_open(const kfs_context_t co, const char *path, struct fuse_file_info *fi)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->open == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->open(co, path, fi);

    KFS_RETURN(ret);
}

static int
pass_unlink(const kfs_context_t co, const char *path)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->unlink == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->unlink(co, path);

    KFS_RETURN(ret);
}

static int
pass_rmdir(const kfs_context_t co, const char *path)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->rmdir == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->rmdir(co, path);

    KFS_RETURN(ret);
}

/**
 * No translation takes place for the path1 argument.
 */
static int
pass_symlink(const kfs_context_t co, const char *path1, const char *path2)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->symlink == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->symlink(co, path1, path2);

    KFS_RETURN(ret);
}

static int
pass_rename(const kfs_context_t co, const char *from, const char *to)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->rename == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->rename(co, from, to);

    KFS_RETURN(ret);
}

static int
pass_link(const kfs_context_t co, const char *from, const char *to)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->link == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->link(co, from, to);

    KFS_RETURN(ret);
}

static int
pass_chmod(const kfs_context_t co, const char *path, mode_t mode)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->chmod == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->chmod(co, path, mode);

    KFS_RETURN(ret);
}

static int
pass_chown(const kfs_context_t co, const char *path, uid_t uid, gid_t gid)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->chown == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->chown(co, path, uid, gid);

    KFS_RETURN(ret);
}

static int
pass_read(const kfs_context_t co, const char *path, char *buf, size_t size,
        off_t offset, struct fuse_file_info *fi)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->read == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->read(co, path, buf, size, offset, fi);

    KFS_RETURN(ret);
}

static int
pass_write(const kfs_context_t co, const char *path, const char *buf, size_t
        size, off_t offset, struct fuse_file_info *fi)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->write == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->write(co, path, buf, size, offset, fi);

    KFS_RETURN(ret);
}

static int
pass_statfs(const kfs_context_t co, const char *path, struct statvfs *stbuf)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->statfs == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->statfs(co, path, stbuf);

    KFS_RETURN(ret);
}

static int
pass_flush(const kfs_context_t co, const char *path, struct fuse_file_info
        *fi)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->flush == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->flush(co, path, fi);

    KFS_RETURN(ret);
}

static int
pass_release(const kfs_context_t co, const char *path, struct fuse_file_info
        *fi)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->release == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->release(co, path, fi);

    KFS_RETURN(ret);
}

static int
pass_fsync(const kfs_context_t co, const char *path, int isdatasync, struct
        fuse_file_info *fi)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->fsync == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->fsync(co, path, isdatasync, fi);

    KFS_RETURN(ret);
}


#ifdef KFS_USE_XATTR
/*
 * Extended attributes.
 */

static int
pass_setxattr(const kfs_context_t co, const char *path, const char *name,
        const char *value, size_t size, int flags)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->setxattr == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->setxattr(co, path, name, value, size, flags);

    KFS_RETURN(ret);
}

static int
pass_getxattr(const kfs_context_t co, const char *path, const char *name, char
        *value, size_t size)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->getxattr == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->getxattr(co, path, name, value, size);

    KFS_RETURN(ret);
}

static int
pass_listxattr(const kfs_context_t co, const char *path, char *list, size_t
        size)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->listxattr == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->listxattr(co, path, list, size);

    KFS_RETURN(ret);
}

static int
pass_removexattr(const kfs_context_t co, const char *path, const char *name)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->removexattr == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->removexattr(co, path, name);

    KFS_RETURN(ret);
}
#endif

/*
 * Directories.
 */

static int
pass_mkdir(const kfs_context_t co, const char *path, mode_t mode)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->mkdir == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->mkdir(co, path, mode);

    KFS_RETURN(ret);
}

static int
pass_opendir(const kfs_context_t co, const char *path, struct fuse_file_info
        *fi)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->opendir == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->opendir(co, path, fi);

    KFS_RETURN(ret);
}

/**
 * List directory contents.
 */
static int
pass_readdir(const kfs_context_t co, const char *path, void *buf,
        fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->readdir == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->readdir(co, path, buf, filler, offset, fi);

    KFS_RETURN(ret);
}

static int
pass_releasedir(const kfs_context_t co, const char *path, struct fuse_file_info *fi)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->releasedir == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->releasedir(co, path, fi);

    KFS_RETURN(ret);
}

static int
pass_fsyncdir(const kfs_context_t co, const char *path, int isdatasync, struct
        fuse_file_info *fi)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->fsyncdir == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->fsyncdir(co, path, isdatasync, fi);

    KFS_RETURN(ret);
}

static int
pass_access(const kfs_context_t co, const char *path, int mask)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->access == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->access(co, path, mask);

    KFS_RETURN(ret);
}

static int
pass_create(const kfs_context_t co, const char *path, mode_t mode, struct
        fuse_file_info *fi)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->create == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->create(co, path, mode, fi);

    KFS_RETURN(ret);
}

static int
pass_ftruncate(const kfs_context_t co, const char *path, off_t size, struct
        fuse_file_info *fi)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->ftruncate == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->ftruncate(co, path, size, fi);
    
    KFS_RETURN(ret);
}

static int
pass_fgetattr(const kfs_context_t co, const char *path, struct stat *stbuf,
        struct fuse_file_info *fi)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->fgetattr == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->fgetattr(co, path, stbuf, fi);

    KFS_RETURN(ret);
}

static int
pass_lock(const kfs_context_t co, const char *path, struct fuse_file_info *fi,
        int cmd, struct flock *lock)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->lock == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->lock(co, path, fi, cmd, lock);

    KFS_RETURN(ret);
}

static int
pass_utimens(const kfs_context_t co, const char *path, const struct timespec
        tvnano[2])
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->utimens == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->utimens(co, path, tvnano);

    KFS_RETURN(ret);
}

static int
pass_bmap(const kfs_context_t co, const char *path, size_t blocksize, uint64_t
        *idx)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->bmap == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->bmap(co, path, blocksize, idx);

    KFS_RETURN(ret);
}

#if FUSE_VERSION >= 28
static int
pass_ioctl(const kfs_context_t co, const char *path, int cmd, void *arg,
        struct fuse_file_info *fi, uint_t flags, void *data)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->ioctl == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->ioctl(co, path, cmd, arg, fi, flags, data);

    KFS_RETURN(ret);
}
#endif

#if FUSE_VERSION >= 28
static int
pass_poll(const kfs_context_t co, const char *path, struct fuse_file_info *fi,
        struct fuse_pollhandle *ph, uint_t *reventsp)
{
    struct kfs_subvolume * const subv = co->private_data;
    const struct kfs_operations * const oper = subv->oper;
    int ret = 0;

    KFS_ENTER();

    co->private_data = subv->private_data;
    if (oper->poll == NULL) {
        KFS_RETURN(-ENOSYS);
    }
    ret = oper->poll(co, path, fi, ph, reventsp);

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
#if KFS_USE_XATTR
    .setxattr = pass_setxattr,
    .getxattr = pass_getxattr,
    .listxattr = pass_listxattr,
    .removexattr = pass_removexattr,
#endif
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
        const struct kfs_subvolume subvolumes[])
{
    (void) conffile;
    (void) section;

    struct kfs_subvolume *subv_copy = NULL;

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
