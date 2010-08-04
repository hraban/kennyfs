/**
 * KennyFS brick that passes through all FUSE operations verbatim. Useful for
 * testing and debugging. Also serves as a template for new bricks.
 */

#define FUSE_USE_VERSION 29

#include "pass_brick/kfs_brick_pass.h"

#include <fuse.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "kfs.h"
#include "kfs_api.h"
#include "kfs_misc.h"

struct kfs_brick_pass_arg {
};

static const struct fuse_operations *oper = NULL;

static int
handle_getattr(const char *path, struct stat *stbuf)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->getattr(path, stbuf);

    KFS_RETURN(ret);
}

static int
handle_readlink(const char *path, char *buf, size_t size)
{
    ssize_t ret = 0;

    KFS_ENTER();

    ret = oper->readlink(path, buf, size);

    KFS_RETURN(ret);
}

static int
handle_mknod(const char *path, mode_t mode, dev_t dev)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->mknod(path, mode, dev);

    KFS_RETURN(ret);
}

static int
handle_truncate(const char *path, off_t offset)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->truncate(path, offset);

    KFS_RETURN(ret);
}

static int
handle_open(const char *path, struct fuse_file_info *fi)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->open(path, fi);

    KFS_RETURN(ret);
}

static int
handle_unlink(const char *path)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->unlink(path);

    KFS_RETURN(ret);
}

static int
handle_rmdir(const char *path)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->rmdir(path);

    KFS_RETURN(ret);
}

/**
 * No translation takes place for the path1 argument.
 */
static int
handle_symlink(const char *path1, const char *path2)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->symlink(path1, path2);

    KFS_RETURN(ret);
}

static int
handle_rename(const char *from, const char *to)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->rename(from, to);

    KFS_RETURN(ret);
}

static int
handle_link(const char *from, const char *to)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->link(from, to);

    KFS_RETURN(ret);
}

static int
handle_chmod(const char *path, mode_t mode)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->chmod(path, mode);

    KFS_RETURN(ret);
}

static int
handle_chown(const char *path, uid_t uid, gid_t gid)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->chown(path, uid, gid);

    KFS_RETURN(ret);
}

static int
handle_read(const char *path, char *buf, size_t size, off_t offset, struct
        fuse_file_info *fi)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->read(path, buf, size, offset, fi);

    KFS_RETURN(ret);
}

static int
handle_write(const char *path, const char *buf, size_t size, off_t offset,
        struct fuse_file_info *fi)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->write(path, buf, size, offset, fi);

    KFS_RETURN(ret);
}

static int
handle_statfs(const char *path, struct statvfs *stbuf)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->statfs(path, stbuf);

    KFS_RETURN(ret);
}

static int
handle_flush(const char *path, struct fuse_file_info *fi)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->flush(path, fi);

    KFS_RETURN(ret);
}

static int
handle_release(const char *path, struct fuse_file_info *fi)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->release(path, fi);

    KFS_RETURN(ret);
}

static int
handle_fsync(const char *path, int isdatasync, struct fuse_file_info *fi)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->fsync(path, isdatasync, fi);

    KFS_RETURN(ret);
}


#ifdef KFS_USE_XATTR
/*
 * Extended attributes.
 */

static int
handle_setxattr(const char *path, const char *name, const char *value, size_t
        size, int flags)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->setxattr(path, name, value, size, flags);

    KFS_RETURN(ret);
}

static int
handle_getxattr(const char *path, const char *name, char *value, size_t size)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->getxattr(path, name, value, size);

    KFS_RETURN(ret);
}

static int
handle_listxattr(const char *path, char *list, size_t size)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->listxattr(path, list, size);

    KFS_RETURN(ret);
}

static int
handle_removexattr(const char *path, const char *name)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->removexattr(path, name);

    KFS_RETURN(ret);
}
#endif

/*
 * Directories.
 */

static int
handle_mkdir(const char *path, mode_t mode)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->mkdir(path, mode);

    KFS_RETURN(ret);
}

static int
handle_opendir(const char *path, struct fuse_file_info *fi)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->opendir(path, fi);

    KFS_RETURN(ret);
}

/**
 * List directory contents.
 */
static int
handle_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
        off_t offset, struct fuse_file_info *fi)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->readdir(path, buf, filler, offset, fi);

    KFS_RETURN(ret);
}

static int
handle_releasedir(const char *path, struct fuse_file_info *fi)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->releasedir(path, fi);

    KFS_RETURN(ret);
}

static int
handle_fsyncdir(const char *path, int isdatasync, struct fuse_file_info *fi)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->fsyncdir(path, isdatasync, fi);

    KFS_RETURN(ret);
}

static void *
handle_init(struct fuse_conn_info *conn)
{
    void *context = NULL;

    KFS_ENTER();

    context = oper->init(conn);

    KFS_RETURN(context);
}

static void
handle_destroy(void *context)
{
    KFS_ENTER();

    oper->destroy(context);

    KFS_RETURN();
}

static int
handle_access(const char *path, int mask)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->access(path, mask);

    KFS_RETURN(ret);
}

static int
handle_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->create(path, mode, fi);

    KFS_RETURN(ret);
}

static int
handle_ftruncate(const char *path, off_t size, struct fuse_file_info *fi)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->ftruncate(path, size, fi);
    
    KFS_RETURN(ret);
}

static int
handle_fgetattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->fgetattr(path, stbuf, fi);

    KFS_RETURN(ret);
}

static int
handle_lock(const char *path, struct fuse_file_info *fi, int cmd,
		    struct flock *lock)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->lock(path, fi, cmd, lock);

    KFS_RETURN(ret);
}

static int
handle_utimens(const char *path, const struct timespec tvnano[2])
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->utimens(path, tvnano);

    KFS_RETURN(ret);
}

static int
handle_bmap(const char *path, size_t blocksize, uint64_t *idx)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->bmap(path, blocksize, idx);

    KFS_RETURN(ret);
}

static int
handle_ioctl(const char *path, int cmd, void *arg, struct fuse_file_info *fi,
        uint_t flags, void *data)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->ioctl(path, cmd, arg, fi, flags, data);

    KFS_RETURN(ret);
}

static int
handle_poll(const char *path, struct fuse_file_info *fi, struct fuse_pollhandle
        *ph, uint_t *reventsp)
{
    int ret = 0;

    KFS_ENTER();

    ret = oper->poll(path, fi, ph, reventsp);

    KFS_RETURN(ret);
}

static const struct fuse_operations handlers = {
    .getattr = handle_getattr,
    .readlink = handle_readlink,
    .mknod = handle_mknod,
    .mkdir = handle_mkdir,
    .unlink = handle_unlink,
    .rmdir = handle_rmdir,
    .symlink = handle_symlink,
    .rename = handle_rename,
    .link = handle_link,
    .chmod = handle_chmod,
    .chown = handle_chown,
    .truncate = handle_truncate,
    .open = handle_open,
    .read = handle_read,
    .write = handle_write,
    .statfs = handle_statfs,
    .flush = handle_flush,
    .release = handle_release,
    .fsync = handle_fsync,
#if KFS_USE_XATTR
    .setxattr = handle_setxattr,
    .getxattr = handle_getxattr,
    .listxattr = handle_listxattr,
    .removexattr = handle_removexattr,
#endif
    .opendir = handle_opendir,
    .readdir = handle_readdir,
    .releasedir = handle_releasedir,
    .fsyncdir = handle_fsyncdir,
    .init = handle_init,
    .destroy = handle_destroy,
    .access = handle_access,
    .create = handle_create,
    .ftruncate = handle_ftruncate,
    .fgetattr = handle_fgetattr,
    .lock = handle_lock,
    .utimens = handle_utimens,
    .bmap = handle_bmap,
    .ioctl = handle_ioctl,
    .poll = handle_poll,
};

/**
 * Global initialization.
 */
static int
kfs_pass_init(const char *conffile, const char *section)
{
    (void) conffile;
    (void) section;

    KFS_ENTER();

    KFS_RETURN(0);
}

/*
 * Get the backend interface.
 */
static const struct fuse_operations *
kfs_pass_getfuncs(void)
{
    KFS_ENTER();

    KFS_RETURN(&handlers);
}

/**
 * Global cleanup.
 */
static void
kfs_pass_halt(void)
{
    KFS_ENTER();

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
