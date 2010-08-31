/**
 * Empty operation handlers that only return a "operation not supported" error
 * code. Useful as filler for bricks that do not support some of the operations.
 */

#include "kfs_nosys.h"

#include <errno.h>

#include "kfs.h"
#include "kfs_api.h"

inline int nosys_getattr(const kfs_context_t c, const char *p, struct stat *s)
{ (void) c; (void) p; (void) s; KFS_ENTER(); KFS_RETURN(-ENOSYS); }
inline int nosys_readlink(const kfs_context_t c, const char *p, char *b, size_t
        s)
{ (void) c; (void) p; (void) b; (void) s; KFS_ENTER(); KFS_RETURN(-ENOSYS); }
inline int nosys_mknod(const kfs_context_t c, const char *p, mode_t m, dev_t d)
{ (void) c; (void) p; (void) m; (void) d; KFS_ENTER(); KFS_RETURN(-ENOSYS); }
inline int nosys_mkdir(const kfs_context_t c, const char *p, mode_t m)
{ (void) c; (void) p; (void) m; KFS_ENTER(); KFS_RETURN(-ENOSYS); }
inline int nosys_unlink(const kfs_context_t c, const char *p)
{ (void) c; (void) p; KFS_ENTER(); KFS_RETURN(-ENOSYS); }
inline int nosys_rmdir(const kfs_context_t c, const char *p)
{ (void) c; (void) p; KFS_ENTER(); KFS_RETURN(-ENOSYS); }
inline int nosys_symlink(const kfs_context_t c, const char *p1, const char *p2)
{ (void) c; (void) p1; (void) p2; KFS_ENTER(); KFS_RETURN(-ENOSYS); }
inline int nosys_rename(const kfs_context_t c, const char *p1, const char *p2)
{ (void) c; (void) p1; (void) p2; KFS_ENTER(); KFS_RETURN(-ENOSYS); }
inline int nosys_link(const kfs_context_t c, const char *p1, const char *p2)
{ (void) c; (void) p1; (void) p2; KFS_ENTER(); KFS_RETURN(-ENOSYS); }
inline int nosys_chmod(const kfs_context_t c, const char *p, mode_t m)
{ (void) c; (void) p; (void) m; KFS_ENTER(); KFS_RETURN(-ENOSYS); }
inline int nosys_chown(const kfs_context_t c, const char *p, uid_t u, gid_t g)
{ (void) c; (void) p; (void) u; (void) g; KFS_ENTER(); KFS_RETURN(-ENOSYS); }
inline int nosys_truncate(const kfs_context_t c, const char *p, off_t o)
{ (void) c; (void) p; (void) o; KFS_ENTER(); KFS_RETURN(-ENOSYS); }
inline int nosys_open(const kfs_context_t c, const char *p, struct
        fuse_file_info *f)
{ (void) c; (void) p; (void) f; KFS_ENTER(); KFS_RETURN(-ENOSYS); }
inline int nosys_read(const kfs_context_t c, const char *p, char *b, size_t s,
        off_t o, struct fuse_file_info *f)
{ (void) c; (void) p; (void) b; (void) s; (void) o; (void) f; KFS_ENTER();
    KFS_RETURN(-ENOSYS); }
inline int nosys_write(const kfs_context_t c, const char *p, const char *b,
        size_t s, off_t o, struct fuse_file_info *f)
{ (void) c; (void) p; (void) b; (void) s; (void) o; (void) f; KFS_ENTER();
    KFS_RETURN(-ENOSYS); }
inline int nosys_statfs(const kfs_context_t c, const char *p, struct statvfs *s)
{ (void) c; (void) p; (void) s; KFS_ENTER(); KFS_RETURN(-ENOSYS); }
inline int nosys_flush(const kfs_context_t c, const char *p, struct
        fuse_file_info *f)
{ (void) c; (void) p; (void) f; KFS_ENTER(); KFS_RETURN(-ENOSYS); }
inline int nosys_release(const kfs_context_t c, const char *p, struct
        fuse_file_info *f)
{ (void) c; (void) p; (void) f; KFS_ENTER(); KFS_RETURN(-ENOSYS); }
inline int nosys_fsync(const kfs_context_t c, const char *p, int i, struct
        fuse_file_info *f)
{ (void) c; (void) p; (void) i; (void) f; KFS_ENTER(); KFS_RETURN(-ENOSYS); }
inline int nosys_setxattr(const kfs_context_t c, const char *p, const char *k,
        const char *v, size_t s, int i)
{ (void) c; (void) p; (void) k; (void) v; (void) s; (void) i; KFS_ENTER();
    KFS_RETURN(-ENOSYS); }
inline int nosys_getxattr(const kfs_context_t c, const char *p, const char *k,
        char *b, size_t s)
{ (void) c; (void) p; (void) k; (void) b; (void) s; KFS_ENTER();
    KFS_RETURN(-ENOSYS); }
inline int nosys_listxattr(const kfs_context_t c, const char *p, char *b, size_t
        s)
{ (void) c; (void) p; (void) b; (void) s; KFS_ENTER(); KFS_RETURN(-ENOSYS); }
inline int nosys_removexattr(const kfs_context_t c, const char *p, const char
        *k)
{ (void) c; (void) p; (void) k; KFS_ENTER(); KFS_RETURN(-ENOSYS); }
inline int nosys_opendir(const kfs_context_t c, const char *p, struct
        fuse_file_info *f)
{ (void) c; (void) p; (void) f; KFS_ENTER(); KFS_RETURN(-ENOSYS); }
inline int nosys_readdir(const kfs_context_t c, const char *p, void *b,
        fuse_fill_dir_t f, off_t o, struct fuse_file_info *fi)
{ (void) c; (void) p; (void) b; (void) f; (void) o; (void) fi; KFS_ENTER();
    KFS_RETURN(-ENOSYS); }
inline int nosys_releasedir(const kfs_context_t c, const char *p, struct
        fuse_file_info *f)
{ (void) c; (void) p; (void) f; KFS_ENTER(); KFS_RETURN(-ENOSYS); }
inline int nosys_fsyncdir(const kfs_context_t c, const char *p, int i, struct
        fuse_file_info *f)
{ (void) c; (void) p; (void) i; (void) f; KFS_ENTER(); KFS_RETURN(-ENOSYS); }
inline int nosys_access(const kfs_context_t c, const char *p, int i)
{ (void) c; (void) p; (void) i; KFS_ENTER(); KFS_RETURN(-ENOSYS); }
inline int nosys_create(const kfs_context_t c, const char *p, mode_t m, struct
        fuse_file_info *f)
{ (void) c; (void) p; (void) m; (void) f; KFS_ENTER(); KFS_RETURN(-ENOSYS); }
inline int nosys_ftruncate(const kfs_context_t c, const char *p, off_t o, struct
        fuse_file_info *f)
{ (void) c; (void) p; (void) o; (void) f; KFS_ENTER(); KFS_RETURN(-ENOSYS); }
inline int nosys_fgetattr(const kfs_context_t c, const char *p, struct stat *s,
        struct fuse_file_info *f)
{ (void) c; (void) p; (void) s; (void) f; KFS_ENTER(); KFS_RETURN(-ENOSYS); }
inline int nosys_lock(const kfs_context_t c, const char *p, struct
        fuse_file_info *f, int i, struct flock *l)
{ (void) c; (void) p; (void) f; (void) i; (void) l; KFS_ENTER();
    KFS_RETURN(-ENOSYS); }
inline int nosys_utimens(const kfs_context_t c, const char *p, const struct
        timespec t[2])
{ (void) c; (void) p; (void) t; KFS_ENTER(); KFS_RETURN(-ENOSYS); }
inline int nosys_bmap(const kfs_context_t c, const char *p, size_t s, uint64_t
        *i)
{ (void) c; (void) p; (void) s; (void) i; KFS_ENTER(); KFS_RETURN(-ENOSYS); }
#if FUSE_VERSION >= 28
inline int nosys_ioctl(const kfs_context_t c, const char *p, int i, void *v,
        struct fuse_file_info *f, uint_t u, void *d)
{ (void) c; (void) p; (void) i; (void) v; (void) f; (void) u; (void) d;
    KFS_ENTER(); KFS_RETURN(-ENOSYS); }
inline int nosys_poll(const kfs_context_t c, const char *p, struct
        fuse_file_info *f, struct fuse_pollhandle *h, uint_t *u)
{ (void) c; (void) p; (void) f; (void) h; (void) u; KFS_ENTER();
    KFS_RETURN(-ENOSYS); }
#endif
