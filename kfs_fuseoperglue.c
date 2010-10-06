/**
 * Translation module between the KennyFS API and the FUSE API for operation
 * handlers. KennyFS operation handlers require state to be explicitly passed
 * with every handler invocation, this is not done by FUSE. The translation
 * layer consists of a pass-through handler for every operation that fetches the
 * context and then calls the KennyFS handler with it, returning whatever that
 * returned.
 */
 
#define FUSE_USE_VERSION 29

#include "kfs_fuseoperglue.h"

#include <fuse.h>

#include "kfs.h"
#include "kfs_api.h"

static const struct kfs_operations *oper = NULL;
static void *root_private_data = NULL;

static inline const kfs_context_t
kfs_get_context(void)
{
    return fuse_get_context();
}

static int root_getattr(const char *p, struct stat *s)
{ int r; KFS_ENTER(); r = oper->getattr(kfs_get_context(), p, s); KFS_RETURN(r);
}
static int root_readlink(const char *p, char *b, size_t s)
{ int r; KFS_ENTER(); r = oper->readlink(kfs_get_context(), p, b, s);
    KFS_RETURN(r); }
static int root_mknod(const char *p, mode_t m, dev_t d)
{ int r; KFS_ENTER(); r = oper->mknod(kfs_get_context(), p, m, d);
    KFS_RETURN(r); }
static int root_mkdir(const char *p, mode_t m)
{ int r; KFS_ENTER(); r = oper->mkdir(kfs_get_context(), p, m); KFS_RETURN(r); }
static int root_unlink(const char *p)
{ int r; KFS_ENTER(); r = oper->unlink(kfs_get_context(), p); KFS_RETURN(r); }
static int root_rmdir(const char *p)
{ int r; KFS_ENTER(); r = oper->rmdir(kfs_get_context(), p); KFS_RETURN(r); }
static int root_symlink(const char *p1, const char *p2)
{ int r; KFS_ENTER(); r = oper->symlink(kfs_get_context(), p1, p2);
    KFS_RETURN(r); }
static int root_rename(const char *p1, const char *p2)
{ int r; KFS_ENTER(); r = oper->rename(kfs_get_context(), p1, p2);
    KFS_RETURN(r); }
static int root_link(const char *p1, const char *p2)
{ int r; KFS_ENTER(); r = oper->link(kfs_get_context(), p1, p2); KFS_RETURN(r);
}
static int root_chmod(const char *p, mode_t m)
{ int r; KFS_ENTER(); r = oper->chmod(kfs_get_context(), p, m); KFS_RETURN(r); }
static int root_chown(const char *p, uid_t u, gid_t g)
{ int r; KFS_ENTER(); r = oper->chown(kfs_get_context(), p, u, g);
    KFS_RETURN(r); }
static int root_truncate(const char *p, off_t o)
{ int r; KFS_ENTER(); r = oper->truncate(kfs_get_context(), p, o);
    KFS_RETURN(r); }
static int root_open(const char *p, struct fuse_file_info *f)
{ int r; KFS_ENTER(); r = oper->open(kfs_get_context(), p, f); KFS_RETURN(r); }
static int root_read(const char *p, char *b, size_t s, off_t o, struct
        fuse_file_info *f)
{ int r; KFS_ENTER(); r = oper->read(kfs_get_context(), p, b, s, o, f);
    KFS_RETURN(r); }
static int root_write(const char *p, const char *b, size_t s, off_t o, struct
        fuse_file_info *f)
{ int r; KFS_ENTER(); r = oper->write(kfs_get_context(), p, b, s, o, f);
    KFS_RETURN(r); }
static int root_statfs(const char *p, struct statvfs *s)
{ int r; KFS_ENTER(); r = oper->statfs(kfs_get_context(), p, s); KFS_RETURN(r);
}
static int root_flush(const char *p, struct fuse_file_info *f)
{ int r; KFS_ENTER(); r = oper->flush(kfs_get_context(), p, f); KFS_RETURN(r); }
static int root_release(const char *p, struct fuse_file_info *f)
{ int r; KFS_ENTER(); r = oper->release(kfs_get_context(), p, f); KFS_RETURN(r);
}
static int root_fsync(const char *p, int i, struct fuse_file_info *f)
{ int r; KFS_ENTER(); r = oper->fsync(kfs_get_context(), p, i, f);
    KFS_RETURN(r); }
static int root_setxattr(const char *p, const char *k, const char *v, size_t s,
        int i)
{ int r; KFS_ENTER(); r = oper->setxattr(kfs_get_context(), p, k, v, s, i);
    KFS_RETURN(r); }
static int root_getxattr(const char *p, const char *k, char *b, size_t s)
{ int r; KFS_ENTER(); r = oper->getxattr(kfs_get_context(), p, k, b, s);
    KFS_RETURN(r); }
static int root_listxattr(const char *p, char *b, size_t s)
{ int r; KFS_ENTER(); r = oper->listxattr(kfs_get_context(), p, b, s);
    KFS_RETURN(r); }
static int root_removexattr(const char *p, const char *k)
{ int r; KFS_ENTER(); r = oper->removexattr(kfs_get_context(), p, k);
    KFS_RETURN(r); }
static int root_opendir(const char *p, struct fuse_file_info *f)
{ int r; KFS_ENTER(); r = oper->opendir(kfs_get_context(), p, f); KFS_RETURN(r);
}
static int root_readdir(const char *p, void *b, fuse_fill_dir_t f, off_t o,
        struct fuse_file_info *fi)
{ int r; KFS_ENTER(); r = oper->readdir(kfs_get_context(), p, b, f, o, fi);
    KFS_RETURN(r); }
static int root_releasedir(const char *p, struct fuse_file_info *f)
{ int r; KFS_ENTER(); r = oper->releasedir(kfs_get_context(), p, f);
    KFS_RETURN(r); }
static int root_fsyncdir(const char *p, int i, struct fuse_file_info *f)
{ int r; KFS_ENTER(); r = oper->fsyncdir(kfs_get_context(), p, i, f);
    KFS_RETURN(r); }
static int root_access(const char *p, int i)
{ int r; KFS_ENTER(); r = oper->access(kfs_get_context(), p, i); KFS_RETURN(r);
}
static int root_create(const char *p, mode_t m, struct fuse_file_info *f)
{ int r; KFS_ENTER(); r = oper->create(kfs_get_context(), p, m, f);
    KFS_RETURN(r); }
static int root_ftruncate(const char *p, off_t o, struct fuse_file_info *f)
{ int r; KFS_ENTER(); r = oper->ftruncate(kfs_get_context(), p, o, f);
    KFS_RETURN(r); }
static int root_fgetattr(const char *p, struct stat *s, struct fuse_file_info
        *f)
{ int r; KFS_ENTER(); r = oper->fgetattr(kfs_get_context(), p, s, f);
    KFS_RETURN(r); }
static int root_lock(const char *p, struct fuse_file_info *f, int i, struct
        flock *l)
{ int r; KFS_ENTER(); r = oper->lock(kfs_get_context(), p, f, i, l);
    KFS_RETURN(r); }
static int root_utimens(const char *p, const struct timespec t[2])
{ int r; KFS_ENTER(); r = oper->utimens(kfs_get_context(), p, t); KFS_RETURN(r);
}
static int root_bmap(const char *p, size_t s, uint64_t *i)
{ int r; KFS_ENTER(); r = oper->bmap(kfs_get_context(), p, s, i); KFS_RETURN(r);
}
#if FUSE_VERSION >= 28
static int root_ioctl(const char *p, int i, void *v, struct fuse_file_info *f,
        uint_t u, void *d)
{ int r; KFS_ENTER(); r = oper->ioctl(kfs_get_context(), p, i, v, f, u, d);
    KFS_RETURN(r); }
static int root_poll(const char *p, struct fuse_file_info *f, struct
        fuse_pollhandle *h, uint_t *u)
{ int r; KFS_ENTER(); r = oper->poll(kfs_get_context(), p, f, h, u);
    KFS_RETURN(r); }
#endif

/**
 * The first operation invoked by FUSE is init(). Its return value is stored as
 * the private_data field of the fuse context. KennyFS does it differently:
 * init() is part of the brick API, it is not an operation. This function
 * handles the translation.
 */
static void *
root_init(struct fuse_conn_info *conn)
{
    (void) conn;

    KFS_ENTER();

    KFS_RETURN(root_private_data);
}

static const struct fuse_operations fuse_oper = {
    .getattr = root_getattr,
    .readlink = root_readlink,
    .mknod = root_mknod,
    .mkdir = root_mkdir,
    .unlink = root_unlink,
    .rmdir = root_rmdir,
    .symlink = root_symlink,
    .rename = root_rename,
    .link = root_link,
    .chmod = root_chmod,
    .chown = root_chown,
    .truncate = root_truncate,
    .open = root_open,
    .read = root_read,
    .write = root_write,
    .statfs = root_statfs,
    .flush = root_flush,
    .release = root_release,
    .fsync = root_fsync,
    .setxattr = root_setxattr,
    .getxattr = root_getxattr,
    .listxattr = root_listxattr,
    .removexattr = root_removexattr,
    .opendir = root_opendir,
    .readdir = root_readdir,
    .releasedir = root_releasedir,
    .fsyncdir = root_fsyncdir,
    .init = root_init,
    .destroy = NULL,
    .access = root_access,
    .create = root_create,
    .ftruncate = root_ftruncate,
    .fgetattr = root_fgetattr,
    .lock = root_lock,
    .utimens = root_utimens,
    .bmap = root_bmap,
#if FUSE_VERSION >= 28
    .ioctl = root_ioctl,
    .poll = root_poll,
    /* All KennyFS bricks must support NULL paths. */
    .flag_nullpath_ok = 1,
#endif
};

/**
 * Generate a collection of operation handlers conforming to the FUSE API that
 * pass all requests down to the given operation handlers conforming to the
 * KennyFS API. I.e.: returns a translation layer between two APIs.
 */
const struct fuse_operations *
kfs2fuse_operations(const struct kfs_operations *kfs_oper,
        void *private_data)
{
    KFS_ENTER();

    KFS_ASSERT(oper == NULL);
    oper = kfs_oper;
    root_private_data = private_data;

    KFS_RETURN(&fuse_oper);
}

const struct fuse_operations *
kfs2fuse_clean(const struct fuse_operations *o)
{
    KFS_NASSERT((void) o);

    KFS_ENTER();

    KFS_ASSERT(oper != NULL && o == &fuse_oper);
    oper = NULL;
    root_private_data = NULL;

    KFS_RETURN(NULL);
}
