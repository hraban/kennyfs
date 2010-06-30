/**
 * Handlers for operations passed down to the kennyfs TCP brick.
 */

#define FUSE_USE_VERSION 26

#include "tcp_brick/handlers.h"

#include <fuse.h>
#include <fuse_opt.h>

#include "kfs.h"
#include "kfs_misc.h"
#include "tcp_brick/tcp_brick.h"

static int
kenny_getattr(const char *fusepath, struct stat *stbuf)
{
    KFS_ENTER();

    KFS_RETURN(-1);
}

/**
 * Read the target of a symlink.
 */
static int
kenny_readlink(const char *fusepath, char *buf, size_t size)
{
    KFS_ENTER();

    KFS_RETURN(-1);
}

static int
kenny_open(const char *fusepath, struct fuse_file_info *fi)
{
    KFS_ENTER();

    KFS_RETURN(-1);
}

/**
 * Read the contents of given file.
 * TODO: Check compliance of return value with API.
 */
static int
kenny_read(const char *fusepath, char *buf, size_t size, off_t offset, struct
        fuse_file_info *fi)
{
    KFS_ENTER();

    KFS_RETURN(-1);
}

/*
 * Extended attributes.
 */

static int
kenny_setxattr(const char *fusepath, const char *name, const char *value, size_t
        size, int flags)
{
    KFS_ENTER();

    KFS_RETURN(-1);
}

static int
kenny_getxattr(const char *fusepath, const char *name, char *value, size_t size)
{
    KFS_ENTER();

    KFS_RETURN(-1);
}

static int
kenny_listxattr(const char *fusepath, char *list, size_t size)
{
    KFS_ENTER();

    KFS_RETURN(-1);
}

static int
kenny_removexattr(const char *fusepath, const char *name)
{
    KFS_ENTER();

    KFS_RETURN(-1);
}

/*
 * Directories.
 */

static int
kenny_opendir(const char *fusepath, struct fuse_file_info *fi)
{
    KFS_ENTER();

    KFS_RETURN(-1);
}

/**
 * List directory contents.
 */
static int
kenny_readdir(const char *fusepath, void *buf, fuse_fill_dir_t filler,
        off_t offset, struct fuse_file_info *fi)
{
    KFS_ENTER();

    KFS_RETURN(-1);
}

static int
kenny_releasedir(const char *fusepath, struct fuse_file_info *fi)
{
    KFS_ENTER();

    KFS_RETURN(-1);
}

/**
 * Update access/modification time.
 */
static int
kenny_utimens(const char *fusepath, const struct timespec tvnano[2])
{
    KFS_ENTER();

    KFS_RETURN(-1);
}

static const struct fuse_operations handlers = {
    .getattr = kenny_getattr,
    .readlink = kenny_readlink,
    .open = kenny_open,
    .read = kenny_read,
    .setxattr = kenny_setxattr,
    .getxattr = kenny_getxattr,
    .listxattr = kenny_listxattr,
    .removexattr = kenny_removexattr,
    .opendir = kenny_opendir,
    .readdir = kenny_readdir,
    .releasedir = kenny_releasedir,
    .utimens = kenny_utimens,
};

const struct fuse_operations *
get_handlers(void)
{
    KFS_ENTER();

    KFS_RETURN(&handlers);
}
