#ifndef KFS_NOSYS_H
#define KFS_NOSYS_H

#include "kfs_api.h"

inline int nosys_getattr(const kfs_context_t c, const char *p, struct stat *s);
inline int nosys_readlink(const kfs_context_t c, const char *p, char *b, size_t
        s);
inline int nosys_mknod(const kfs_context_t c, const char *p, mode_t m, dev_t d);
inline int nosys_mkdir(const kfs_context_t c, const char *p, mode_t m);
inline int nosys_unlink(const kfs_context_t c, const char *p);
inline int nosys_rmdir(const kfs_context_t c, const char *p);
inline int nosys_symlink(const kfs_context_t c, const char *p1, const char *p2);
inline int nosys_rename(const kfs_context_t c, const char *p1, const char *p2);
inline int nosys_link(const kfs_context_t c, const char *p1, const char *p2);
inline int nosys_chmod(const kfs_context_t c, const char *p, mode_t m);
inline int nosys_chown(const kfs_context_t c, const char *p, uid_t u, gid_t g);
inline int nosys_truncate(const kfs_context_t c, const char *p, off_t o);
inline int nosys_open(const kfs_context_t c, const char *p, struct
        fuse_file_info *f);
inline int nosys_read(const kfs_context_t c, const char *p, char *b, size_t s,
        off_t o, struct fuse_file_info *f);
inline int nosys_write(const kfs_context_t c, const char *p, const char *b,
        size_t s, off_t o, struct fuse_file_info *f);
inline int nosys_statfs(const kfs_context_t c, const char *p, struct statvfs
        *s);
inline int nosys_flush(const kfs_context_t c, const char *p, struct
        fuse_file_info *f);
inline int nosys_release(const kfs_context_t c, const char *p, struct
        fuse_file_info *f);
inline int nosys_fsync(const kfs_context_t c, const char *p, int i, struct
        fuse_file_info *f);
inline int nosys_setxattr(const kfs_context_t c, const char *p, const char *k,
        const char *v, size_t s, int i);
inline int nosys_getxattr(const kfs_context_t c, const char *p, const char *k,
        char *b, size_t s);
inline int nosys_listxattr(const kfs_context_t c, const char *p, char *b, size_t
        s);
inline int nosys_removexattr(const kfs_context_t c, const char *p, const char
        *k);
inline int nosys_opendir(const kfs_context_t c, const char *p, struct
        fuse_file_info *f);
inline int nosys_readdir(const kfs_context_t c, const char *p, void *b,
        fuse_fill_dir_t f, off_t o, struct fuse_file_info *fi);
inline int nosys_releasedir(const kfs_context_t c, const char *p, struct
        fuse_file_info *f);
inline int nosys_fsyncdir(const kfs_context_t c, const char *p, int i, struct
        fuse_file_info *f);
inline int nosys_access(const kfs_context_t c, const char *p, int i);
inline int nosys_create(const kfs_context_t c, const char *p, mode_t m, struct
        fuse_file_info *f);
inline int nosys_ftruncate(const kfs_context_t c, const char *p, off_t o, struct
        fuse_file_info *f);
inline int nosys_fgetattr(const kfs_context_t c, const char *p, struct stat *s,
        struct fuse_file_info *f);
inline int nosys_lock(const kfs_context_t c, const char *p, struct
        fuse_file_info *f, int i, struct flock *l);
inline int nosys_utimens(const kfs_context_t c, const char *p, const struct
        timespec t[2]);
inline int nosys_bmap(const kfs_context_t c, const char *p, size_t s, uint64_t
        *i);
#if FUSE_VERSION >= 28
inline int nosys_ioctl(const kfs_context_t c, const char *p, int i, void *v,
        struct fuse_file_info *f, uint_t u, void *d);
inline int nosys_poll(const kfs_context_t c, const char *p, struct
        fuse_file_info *f, struct fuse_pollhandle *h, uint_t *u);
#endif

#endif
