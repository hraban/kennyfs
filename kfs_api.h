#ifndef KFS_API_H
#define KFS_API_H

#include <errno.h>
/* Typedef size_t is needed. */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
/* In C99 stddef.h is a legal (lighter) substitute to get size_t. */
#  include <stddef.h>
#else
#  include <stdlib.h>
#endif

#include <fuse.h>

#include "kfs.h"

/**
 * Context of a call to an operation handler, including state of the brick as
 * initialised by init(). Every call to a KennyFS operation handler must be
 * accompanied by a context value. The context is valid for the entire duration
 * of the call, but not longer. I.e.: it can be passed down to subvolumes.
 * However, the priv field (void *) is per-subvolume data: it will be set to the
 * return value of init() by the caller and must be set to the respective
 * subvolume's private data when calling its operation handler.
 * 
 * Also see the FUSE documentation.
 */
struct kfs_context {
    uid_t uid;
    gid_t gid;
    void *priv;
};
typedef struct kfs_context * kfs_context_t;

/**
 * All filesystem operations that can be used by a brick to communicate with a
 * subvolume. A brick exports all its operation handlers as a single struct of
 * this type. It is very similar to the struct fuse_operations from the FUSE
 * project, the difference being that every operation handler takes one extra
 * leading argument: a context struct normally acquired through getcontext();.
 *
 * The reason for this customisation is to avoid global variables while
 * retaining per-brick state.
 *
 * FUSE does not need this because, normally, the operation handlers are only
 * one layer deep, and thus only one state need be preserved. KennyFS allows
 * multiple bricks, even of the same type, and this introduces a problem: once a
 * handler is called without context, it is difficult to determine from which
 * brick it operates. Loading a type of brick twice with different states will
 * make the state of the latter load override that of the former, if stored as a
 * global. Making the caller responsible for preserving state of the loaded
 * brick solves this issue.
 *
 * Another difference is that there are no init and destroy operation handlers:
 * that is already implemented in the wrapping KennyFS API (which would be
 * unnecessary if FUSE supported raising an error from the init() routine). 
 */
struct kfs_operations {
    int (*getattr) (kfs_context_t, const char *, struct stat *);
    int (*readlink) (kfs_context_t, const char *, char *, size_t);
    int (*mknod) (kfs_context_t, const char *, mode_t, dev_t);
    int (*mkdir) (kfs_context_t, const char *, mode_t);
    int (*unlink) (kfs_context_t, const char *);
    int (*rmdir) (kfs_context_t, const char *);
    int (*symlink) (kfs_context_t, const char *, const char *);
    int (*rename) (kfs_context_t, const char *, const char *);
    int (*link) (kfs_context_t, const char *, const char *);
    int (*chmod) (kfs_context_t, const char *, mode_t);
    int (*chown) (kfs_context_t, const char *, uid_t, gid_t);
    int (*truncate) (kfs_context_t, const char *, off_t);
    int (*open) (kfs_context_t, const char *, struct fuse_file_info *);
    int (*read) (kfs_context_t, const char *, char *, size_t, off_t, struct
            fuse_file_info *);
    int (*write) (kfs_context_t, const char *, const char *, size_t, off_t,
            struct fuse_file_info *);
    int (*statfs) (kfs_context_t, const char *, struct statvfs *);
    int (*flush) (kfs_context_t, const char *, struct fuse_file_info *);
    int (*release) (kfs_context_t, const char *, struct fuse_file_info *);
    int (*fsync) (kfs_context_t, const char *, int, struct fuse_file_info *);
    int (*setxattr) (kfs_context_t, const char *, const char *, const char *,
            size_t, int);
    int (*getxattr) (kfs_context_t, const char *, const char *, char *, size_t);
    int (*listxattr) (kfs_context_t, const char *, char *, size_t);
    int (*removexattr) (kfs_context_t, const char *, const char *);
    int (*opendir) (kfs_context_t, const char *, struct fuse_file_info *);
    int (*readdir) (kfs_context_t, const char *, void *, fuse_fill_dir_t, off_t,
            struct fuse_file_info *);
    int (*releasedir) (kfs_context_t, const char *, struct fuse_file_info *);
    int (*fsyncdir) (kfs_context_t, const char *, int, struct fuse_file_info *);
    int (*access) (kfs_context_t, const char *, int);
    int (*create) (kfs_context_t, const char *, mode_t, struct fuse_file_info
            *);
    int (*ftruncate) (kfs_context_t, const char *, off_t, struct fuse_file_info
            *);
    int (*fgetattr) (kfs_context_t, const char *, struct stat *, struct
            fuse_file_info *);
    int (*lock) (kfs_context_t, const char *, struct fuse_file_info *, int cmd,
            struct flock *);
    int (*utimens) (kfs_context_t, const char *, const struct timespec tv[2]);
    int (*bmap) (kfs_context_t, const char *, size_t blocksize, uint64_t *idx);
#if FUSE_VERSION >= 28
    int (*ioctl) (kfs_context_t, const char *, int cmd, void *arg, struct
            fuse_file_info *, uint_t flags, void *data);
    int (*poll) (kfs_context_t, const char *, struct fuse_file_info *, struct
            fuse_pollhandle *ph, uint_t *reventsp);
#endif
};

/**
 * A subvolume is a brick that is used by another brick as part of its
 * operations. A subvolume is, technically, a brick, but in the context of being
 * used by another brick it only concerns the API between those two: the
 * operation handlers and the private data of the context of the subvolume.
 */
struct kfs_subvolume {
    const struct kfs_operations *oper;
    void *private_data;
};

/**
 * Function that prepares the brick for operation. The first argument is the
 * path to the configuration file, the second is the section from which this
 * brick is being loaded, i.e.: the name of the brick. They are intended for use
 * with the minini library (linked). The last argument is an array of pointers
 * to the handlers of this brick's subvolumes. The array is terminated by a NULL
 * pointer. On error, NULL is returned. On success, the (non-NULL) value is
 * stored by the caller and passed to every subsequent operation handler as the
 * "priv" field of the context struct. If a brick does not need state it may
 * return a meaningless value to indicate success, as long as it is not NULL,
 * and it need never dereference it hereafter.
 */
typedef void * (* kfs_brick_init_f)(const char *conffile, const char *section,
        size_t num_subvolumes, const struct kfs_subvolume subvolumes[]);
/** Function to obtain pointer to operation callback handlers for this brick. */
typedef const struct kfs_operations * (* kfs_brick_getfuncs_f)(void);
/**
 * Function that shuts down the brick (at least until the next init()). The
 * argument is the void * returned by the init function.
 */
typedef void (* kfs_brick_halt_f)(void *private_data);

struct kfs_brick_api {
    kfs_brick_init_f init;
    kfs_brick_getfuncs_f getfuncs;
    kfs_brick_halt_f halt;
};

/**
 * Function that gets API (above three) of a brick. Every KennyFS brick should
 * provide a public function of this type with the name "kfs_brick_getapi".
 */
typedef const struct kfs_brick_api * (* kfs_brick_getapi_f)(void);

/* The fact that this is a separate file is an implementation detail. */
#include "kfs_nosys.h"

#endif
