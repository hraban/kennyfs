/**
 * KennyFS backend forwarding everything to a locally mounted POSIX-compliant
 * directory.
 */

#define FUSE_USE_VERSION 29
/* Macro is necessary to get fstatat(). */
#define _ATFILE_SOURCE
/* Macro is necessary to get pread(). */
#define _XOPEN_SOURCE 500
/* Macro is necessary to get dirfd(). */
#define _BSD_SOURCE

#include "posix_brick/kfs_brick_posix.h"

/* <attr/xattr.h> needs this header. silly xattr.h. */
#include <sys/types.h>

#ifdef __APPLE__
#  include <sys/xattr.h>
#  define lgetxattr(p, n, v, s) \
    getxattr((p), (n), (v), (s), 0, XATTR_NOFOLLOW)
#  define lsetxattr(p, n, v, s, f) \
    setxattr((p), (n), (v), (s), 0, (f) | XATTR_NOFOLLOW)
#  define llistxattr(p, l, s) \
    listxattr((p), (l), (s), XATTR_NOFOLLOW)
#  define lremovexattr(p, n) \
    removexattr((p), (n), XATTR_NOFOLLOW)
#else
#  include <attr/xattr.h>
#endif
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <utime.h>

#include "minini/minini.h"

#include "kfs.h"
#include "kfs_api.h"
#include "kfs_misc.h"

#if _POSIX_C_SOURCE >= 199309L || _XOPEN_SOURCE >= 500
#  define KFS_USE_FDATASYNC
#endif

/**
 * Free given string buffer if it does not equal given static buffer. Useful for
 * cleaning up potential allocations by kfs_bufstrcat(). Returns the strbuf,
 * either free'd or untouched (just don't use it after this).
 */
#define KFS_BUFSTRFREE(strbuf, staticbuf) (((strbuf) == (staticbuf)) \
                                                    ? (strbuf) \
                                                    : KFS_FREE(strbuf))

/*
 * Operation handlers.
 */

static int
posix_getattr(const kfs_context_t co, const char *fusepath, struct stat *stbuf)
{
    const char * const mountroot = co->priv;
    int ret = 0;
    /* On-stack buffer for paths of limited length. Otherwise: malloc(). */
    char pathbuf[PATHBUF_SIZE];
    char *fullpath = NULL;

    KFS_ENTER();

    KFS_ASSERT(fusepath[0] == '/');
    fullpath = kfs_bufstrcat(pathbuf, mountroot, fusepath, NUMELEM(pathbuf));
    if (fullpath == NULL) {
        KFS_RETURN(-errno);
    }
    ret = lstat(fullpath, stbuf);
    if (ret == -1) {
        ret = -errno;
    }
    fullpath = KFS_BUFSTRFREE(fullpath, pathbuf);

    KFS_RETURN(ret);
}

static int
posix_access(const kfs_context_t co, const char *fusepath, int mask)
{
    const char * const mountroot = co->priv;
    char pathbuf[PATHBUF_SIZE];
    char *fullpath = NULL;
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(fusepath[0] == '/');
    fullpath = kfs_bufstrcat(pathbuf, mountroot, fusepath, NUMELEM(pathbuf));
    if (fullpath == NULL) {
        KFS_RETURN(-errno);
    }
    ret = access(fullpath, mask);
    if (ret == -1) {
        ret = -errno;
    }
    fullpath = KFS_BUFSTRFREE(fullpath, pathbuf);

    KFS_RETURN(ret);
}

static int
posix_create(const kfs_context_t co, const char *fusepath, mode_t mode, struct
        fuse_file_info *fi)
{
    const char * const mountroot = co->priv;
    char pathbuf[PATHBUF_SIZE];
    char *fullpath = NULL;
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(fusepath[0] == '/');
    ret = 0;
    fullpath = kfs_bufstrcat(pathbuf, mountroot, fusepath, NUMELEM(pathbuf));
    if (fullpath == NULL) {
        KFS_RETURN(-errno);
    }
    ret = open(fullpath, fi->flags, mode);
    if (ret == -1) {
        ret = -errno;
    } else {
        fi->fh = ret;
        ret = 0;
    }
    fullpath = KFS_BUFSTRFREE(fullpath, pathbuf);

    KFS_RETURN(ret);
}


static int
posix_ftruncate(const kfs_context_t co, const char *fusepath, off_t off, struct
        fuse_file_info *fi)
{
    (void) co;
    KFS_NASSERT((void) fusepath);

    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(fusepath[0] == '/');
    ret = ftruncate(fi->fh, off);
    if (ret == -1) {
        KFS_RETURN(-errno);
    }

    KFS_RETURN(0);
}

static int
posix_fgetattr(const kfs_context_t co, const char *fusepath, struct stat *stbuf,
        struct fuse_file_info *fi)
{
    (void) co;
    KFS_NASSERT((void) fusepath);

    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(fusepath[0] == '/');
    ret = fstat(fi->fh, stbuf);
    if (ret == -1) {
        KFS_RETURN(-errno);
    }

    KFS_RETURN(0);
}

/**
 * Read the target of a symlink.
 */
static int
posix_readlink(const kfs_context_t co, const char *fusepath, char *buf, size_t
        size)
{
    const char * const mountroot = co->priv;
    char pathbuf[PATHBUF_SIZE];
    ssize_t ret = 0;
    char *fullpath = NULL;

    KFS_ENTER();

    KFS_ASSERT(fusepath[0] == '/');
    fullpath = kfs_bufstrcat(pathbuf, mountroot, fusepath, NUMELEM(pathbuf));
    if (fullpath == NULL) {
        KFS_RETURN(-errno);
    }
    /* Save room for the \0 byte. */
    ret = readlink(fullpath, buf, size - 1);
    if (ret == -1) {
        ret = -errno;
    } else {
        buf[ret] = '\0';
        ret = 0;
    }
    fullpath = KFS_BUFSTRFREE(fullpath, pathbuf);

    KFS_RETURN(ret);
}

static int
posix_mknod(const kfs_context_t co, const char *fusepath, mode_t mode, dev_t
        dev)
{
    const char * const mountroot = co->priv;
    char pathbuf[PATHBUF_SIZE];
    int ret = 0;
    char *fullpath = NULL;

    KFS_ENTER();

    KFS_ASSERT(fusepath[0] == '/');
    fullpath = kfs_bufstrcat(pathbuf, mountroot, fusepath, NUMELEM(pathbuf));
    if (fullpath == NULL) {
        ret = -errno;
        KFS_RETURN(-errno);
    }
    ret = mknod(fullpath, mode, dev);
    if (ret == -1) {
        ret = -errno;
    }
    fullpath = KFS_BUFSTRFREE(fullpath, pathbuf);

    KFS_RETURN(ret);
}

static int
posix_truncate(const kfs_context_t co, const char *fusepath, off_t offset)
{
    const char * const mountroot = co->priv;
    char pathbuf[PATHBUF_SIZE];
    int ret = 0;
    char *fullpath = NULL;

    KFS_ENTER();

    KFS_ASSERT(fusepath[0] == '/');
    fullpath = kfs_bufstrcat(pathbuf, mountroot, fusepath, NUMELEM(pathbuf));
    if (fullpath == NULL) {
        KFS_RETURN(-errno);
    }
    ret = truncate(fullpath, offset);
    if (ret == -1) {
        ret = -errno;
    }
    fullpath = KFS_BUFSTRFREE(fullpath, pathbuf);

    KFS_RETURN(ret);
}

static int
posix_open(const kfs_context_t co, const char *fusepath, struct fuse_file_info
        *fi)
{
    const char * const mountroot = co->priv;
    char pathbuf[PATHBUF_SIZE];
    char *fullpath = NULL;
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(fusepath[0] == '/');
    fullpath = kfs_bufstrcat(pathbuf, mountroot, fusepath, NUMELEM(pathbuf));
    if (fullpath == NULL) {
        KFS_RETURN(-errno);
    }
    ret = open(fullpath, fi->flags);
    if (ret == -1) {
        ret = -errno;
    } else {
        fi->fh = ret;
        ret = 0;
    }
    fullpath = KFS_BUFSTRFREE(fullpath, pathbuf);

    KFS_RETURN(ret);
}

static int
posix_unlink(const kfs_context_t co, const char *fusepath)
{
    const char * const mountroot = co->priv;
    char pathbuf[PATHBUF_SIZE];
    char *fullpath = NULL;
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(fusepath[0] == '/');
    fullpath = kfs_bufstrcat(pathbuf, mountroot, fusepath, NUMELEM(pathbuf));
    if (fullpath == NULL) {
        KFS_RETURN(-errno);
    }
    ret = unlink(fullpath);
    if (ret == -1) {
        ret = -errno;
    }
    fullpath = KFS_BUFSTRFREE(fullpath, pathbuf);

    KFS_RETURN(ret);
}

static int
posix_rmdir(const kfs_context_t co, const char *fusepath)
{
    const char * const mountroot = co->priv;
    char pathbuf[PATHBUF_SIZE];
    char *fullpath = NULL;
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(fusepath[0] == '/');
    fullpath = kfs_bufstrcat(pathbuf, mountroot, fusepath, NUMELEM(pathbuf));
    if (fullpath == NULL) {
        KFS_RETURN(-errno);
    }
    ret = rmdir(fullpath);
    if (ret == -1) {
        ret = -errno;
    }
    fullpath = KFS_BUFSTRFREE(fullpath, pathbuf);

    KFS_RETURN(ret);
}

/**
 * No translation takes place for the path1 argument.
 */
static int
posix_symlink(const kfs_context_t co, const char *path1, const char *path2)
{
    const char * const mountroot = co->priv;
    char pathbuf[PATHBUF_SIZE];
    char *fullpath = NULL;
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(path2[0] == '/');
    fullpath = kfs_bufstrcat(pathbuf, mountroot, path2, NUMELEM(pathbuf));
    ret = symlink(path1, fullpath);
    if (ret == -1) {
        ret = -errno;
    }
    fullpath = KFS_BUFSTRFREE(fullpath, pathbuf);

    KFS_RETURN(ret);
}

static int
posix_rename(const kfs_context_t co, const char *from, const char *to)
{
    const char * const mountroot = co->priv;
    char pathbuf_from[PATHBUF_SIZE];
    char pathbuf_to[PATHBUF_SIZE];
    char *fullpath_from = NULL;
    char *fullpath_to = NULL;
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(from[0] == '/' && to[0] == '/');
    fullpath_from = kfs_bufstrcat(pathbuf_from, mountroot, from,
            NUMELEM(pathbuf_from));
    fullpath_to = kfs_bufstrcat(pathbuf_to, mountroot, to,
            NUMELEM(pathbuf_to));
    ret = rename(fullpath_from, fullpath_to);
    if (ret == -1) {
        ret = -errno;
    }
    fullpath_from = KFS_BUFSTRFREE(fullpath_from, pathbuf_from);
    fullpath_to = KFS_BUFSTRFREE(fullpath_to, pathbuf_to);

    KFS_RETURN(ret);
}

static int
posix_link(const kfs_context_t co, const char *from, const char *to)
{
    const char * const mountroot = co->priv;
    char pathbuf_from[PATHBUF_SIZE];
    char pathbuf_to[PATHBUF_SIZE];
    char *fullpath_from = NULL;
    char *fullpath_to = NULL;
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(from[0] == '/' && to[0] == '/');
    fullpath_from = kfs_bufstrcat(pathbuf_from, mountroot, from,
            NUMELEM(pathbuf_from));
    fullpath_to = kfs_bufstrcat(pathbuf_to, mountroot, to,
            NUMELEM(pathbuf_to));
    ret = link(fullpath_from, fullpath_to);
    if (ret == -1) {
        ret = -errno;
    }
    fullpath_from = KFS_BUFSTRFREE(fullpath_from, pathbuf_from);
    fullpath_to = KFS_BUFSTRFREE(fullpath_to, pathbuf_to);

    KFS_RETURN(ret);
}

static int
posix_chmod(const kfs_context_t co, const char *fusepath, mode_t mode)
{
    const char * const mountroot = co->priv;
    char pathbuf[PATHBUF_SIZE];
    char *fullpath = NULL;
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(fusepath[0] == '/');
    fullpath = kfs_bufstrcat(pathbuf, mountroot, fusepath, NUMELEM(pathbuf));
    if (fullpath == NULL) {
        KFS_RETURN(-errno);
    }
    ret = chmod(fullpath, mode);
    if (ret == -1) {
        ret = -errno;
    }
    fullpath = KFS_BUFSTRFREE(fullpath, pathbuf);

    KFS_RETURN(ret);
}

static int
posix_chown(const kfs_context_t co, const char *fusepath, uid_t uid, gid_t gid)
{
    const char * const mountroot = co->priv;
    char pathbuf[PATHBUF_SIZE];
    char *fullpath = NULL;
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(fusepath[0] == '/');
    fullpath = kfs_bufstrcat(pathbuf, mountroot, fusepath, NUMELEM(pathbuf));
    if (fullpath == NULL) {
        KFS_RETURN(-errno);
    }
    ret = lchown(fullpath, uid, gid);
    if (ret == -1) {
        ret = -errno;
    }
    fullpath = KFS_BUFSTRFREE(fullpath, pathbuf);

    KFS_RETURN(ret);
}



/**
 * Read the contents of given file.
 */
static int
posix_read(const kfs_context_t co, const char *fusepath, char *buf, size_t size,
        off_t offset, struct fuse_file_info *fi)
{
    (void) co;
    KFS_NASSERT((void) fusepath);

    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(fusepath[0] == '/');
    ret = pread(fi->fh, buf, size, offset);
    if (ret == -1) {
        ret = -errno;
    }

    KFS_RETURN(ret);
}

/**
 * Write to a file.
 */
static int
posix_write(const kfs_context_t co, const char *fusepath, const char *buf,
        size_t size, off_t offset,
        struct fuse_file_info *fi)
{
    (void) co;
    KFS_NASSERT((void) fusepath);

    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(fusepath[0] == '/');
    ret = pwrite(fi->fh, buf, size, offset);
    if (ret == -1) {
        KFS_RETURN(-errno);
    }

    KFS_RETURN(ret);
}

static int
posix_statfs(const kfs_context_t co, const char *fusepath, struct statvfs *buf)
{
    (void) co;

    const char * const mountroot = co->priv;
    char pathbuf[PATHBUF_SIZE];
    char *fullpath = NULL;
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(fusepath[0] == '/');
    fullpath = kfs_bufstrcat(pathbuf, mountroot, fusepath, NUMELEM(pathbuf));
    if (fullpath == NULL) {
        KFS_RETURN(-errno);
    }
    ret = statvfs(fullpath, buf);
    if (ret == -1) {
        ret = -errno;
    }
    fullpath = KFS_BUFSTRFREE(fullpath, pathbuf);

    KFS_RETURN(0);
}

/**
 * Flush the data from the filesystem buffers to the subvolume/OS.
 *
 * Does not imply a sync: just makes sure that the subvolume/OS knows about this
 * data).
 */
static int
posix_flush(const kfs_context_t co, const char *fusepath, struct fuse_file_info
        *fi)
{
    (void) co;
    KFS_NASSERT((void) path);

    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(fusepath[0] == '/');
    /** This is a POSIX equivalent to flushing data to a OS without closing. */
    ret = dup(fi->fh);
    if (ret != -1) {
        ret = close(ret);
        if (ret == 0) {
            KFS_RETURN(0);
        }
    }

    KFS_RETURN(-errno);
}

static int
posix_release(const kfs_context_t co, const char *fusepath, struct fuse_file_info
        *fi)
{
    (void) co;
    KFS_NASSERT((void) fusepath);

    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(fusepath[0] == '/');
    ret = close(fi->fh);
    if (ret == -1) {
        KFS_RETURN(-errno);
    }

    KFS_RETURN(0);
}

static int
posix_fsync(const kfs_context_t co, const char *fusepath, int datasync, struct
        fuse_file_info *fi)
{
    (void) co;
#ifndef KFS_USE_FDATASYNC
    (void) datasync;
#endif
    KFS_NASSERT((void) fusepath);

    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(fusepath[0] == '/');
#ifdef KFS_USE_FDATASYNC
    if (datasync) {
        ret = fdatasync(fi->fh);
    } else
#else
    {
        ret = fsync(fi->fh);
    }
#endif
    if (ret == -1) {
        KFS_RETURN(-errno);
    }

    KFS_RETURN(0);
}


/*
 * Extended attributes.
 */

static int
posix_setxattr(const kfs_context_t co, const char *fusepath, const char *name,
        const char *value, size_t size, int flags)
{
    const char * const mountroot = co->priv;
    char pathbuf[PATHBUF_SIZE];
    int ret = 0;
    char *fullpath = NULL;

    KFS_ENTER();

    KFS_ASSERT(fusepath[0] == '/');
    fullpath = kfs_bufstrcat(pathbuf, mountroot, fusepath, NUMELEM(pathbuf));
    if (fullpath == NULL) {
        KFS_RETURN(-errno);
    }
    ret = lsetxattr(fullpath, name, value, size, flags);
    if (ret == -1) {
        ret = -errno;
    }
    fullpath = KFS_BUFSTRFREE(fullpath, pathbuf);

    KFS_RETURN(ret);
}

static int
posix_getxattr(const kfs_context_t co, const char *fusepath, const char *name,
        char *value, size_t size)
{
    const char * const mountroot = co->priv;
    char pathbuf[PATHBUF_SIZE];
    int ret = 0;
    char *fullpath = NULL;

    KFS_ENTER();

    KFS_ASSERT(fusepath[0] == '/');
    fullpath = kfs_bufstrcat(pathbuf, mountroot, fusepath, NUMELEM(pathbuf));
    if (fullpath == NULL) {
        KFS_RETURN(-errno);
    }
    ret = lgetxattr(fullpath, name, value, size);
    if (ret == -1) {
        ret = -errno;
    }
    fullpath = KFS_BUFSTRFREE(fullpath, pathbuf);

    KFS_RETURN(ret);
}

static int
posix_listxattr(const kfs_context_t co, const char *fusepath, char *list, size_t
        size)
{
    const char * const mountroot = co->priv;
    char pathbuf[PATHBUF_SIZE];
    ssize_t ret = 0;
    char *fullpath = NULL;

    KFS_ENTER();

    KFS_ASSERT(fusepath[0] == '/');
    fullpath = kfs_bufstrcat(pathbuf, mountroot, fusepath, NUMELEM(pathbuf));
    if (fullpath == NULL) {
        KFS_RETURN(-errno);
    }
    ret = llistxattr(fullpath, list, size);
    if (ret == -1) {
        ret = -errno;
    }
    fullpath = KFS_BUFSTRFREE(fullpath, pathbuf);

    KFS_RETURN(ret);
}

static int
posix_removexattr(const kfs_context_t co, const char *fusepath, const char
        *name)
{
    const char * const mountroot = co->priv;
    char pathbuf[PATHBUF_SIZE];
    int ret = 0;
    char *fullpath = NULL;

    KFS_ENTER();

    KFS_ASSERT(fusepath[0] == '/');
    fullpath = kfs_bufstrcat(pathbuf, mountroot, fusepath, NUMELEM(pathbuf));
    if (fullpath == NULL) {
        ret = -errno;
    }
    ret = lremovexattr(fullpath, name);
    if (ret == -1) {
        ret = -errno;
    }
    fullpath = KFS_BUFSTRFREE(fullpath, pathbuf);

    KFS_RETURN(ret);
}

/*
 * Directories.
 */

static int
posix_mkdir(const kfs_context_t co, const char *fusepath, mode_t mode)
{
    const char * const mountroot = co->priv;
    char pathbuf[PATHBUF_SIZE];
    int ret = 0;
    char *fullpath = NULL;

    KFS_ENTER();

    KFS_ASSERT(fusepath[0] == '/');
    fullpath = kfs_bufstrcat(pathbuf, mountroot, fusepath, NUMELEM(pathbuf));
    if (fullpath == NULL) {
        KFS_RETURN(-errno);
    }
    ret = mkdir(fullpath, mode);
    if (ret == -1) {
        ret = -errno;
    }
    fullpath = KFS_BUFSTRFREE(fullpath, pathbuf);

    KFS_RETURN(ret);
}

static int
posix_opendir(const kfs_context_t co, const char *fusepath, struct
        fuse_file_info *fi)
{
    const char * const mountroot = co->priv;
    char pathbuf[PATHBUF_SIZE];
    char *fullpath = NULL;
    DIR *dir = NULL;
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(fusepath[0] == '/');
    fullpath = kfs_bufstrcat(pathbuf, mountroot, fusepath, NUMELEM(pathbuf));
    if (fullpath == NULL) {
        KFS_RETURN(-errno);
    }
    dir = opendir(fullpath);
    if (dir == NULL) {
        ret = -errno;
    } else {
        KFS_ASSERT(sizeof(dir) <= sizeof(fi->fh));
        memcpy(&fi->fh, &dir, sizeof(dir));
    }
    fullpath = KFS_BUFSTRFREE(fullpath, pathbuf);

    KFS_RETURN(ret);
}

/**
 * List directory contents.
 */
static int
posix_readdir(const kfs_context_t co, const char *fusepath, void *buf,
        fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    (void) co;
    KFS_NASSERT((void) fusepath);

    struct stat stbuf;
    struct stat *stbufp = NULL;
    DIR *dir = NULL;
    struct dirent *de = NULL;
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(fusepath[0] == '/');
    memcpy(&dir, &fi->fh, sizeof(dir));
    seekdir(dir, offset);
    for (;;) {
        /* Get an entry. */
        errno = 0;
        de = readdir(dir);
        if (de == NULL) {
            KFS_RETURN(-errno); /* This is probably 0, but that is just fine. */
        }
        /* Stat that entry. TODO: this is not POSIX compliant. */
        stbufp = &stbuf;
        ret = fstatat(dirfd(dir), de->d_name, stbufp, AT_SYMLINK_NOFOLLOW);
        if (ret == -1) {
            stbufp = NULL;
            KFS_WARNING("fstatat: %s", strerror(errno));
        }
        /* Add it to the return-buffer. */
        ret = filler(buf, de->d_name, stbufp, telldir(dir));
        if (ret == 1) {
            KFS_RETURN(0);
        }
    }

    /* Control never reaches this point. */
    KFS_ASSERT(0);
    KFS_RETURN(-1);
}

static int
posix_releasedir(const kfs_context_t co, const char *fusepath, struct
        fuse_file_info *fi)
{
    (void) co;
    KFS_NASSERT((void) fusepath);

    DIR *dir = NULL;
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(fusepath[0] == '/');
    memcpy(&dir, &fi->fh, sizeof(dir));
    ret = closedir(dir);
    if (ret == -1) {
        KFS_RETURN(-errno);
    }

    KFS_RETURN(0);
}

/**
 * Update access/modification time.
 */
static int
posix_utimens(const kfs_context_t co, const char *fusepath, const struct
        timespec tvnano[2])
{
    const char * const mountroot = co->priv;
    char pathbuf[PATHBUF_SIZE];
    struct timeval tvmicro[2];
    int ret = 0;
    char *fullpath = NULL;

    KFS_ENTER();

    KFS_ASSERT(fusepath[0] == '/');
    fullpath = kfs_bufstrcat(pathbuf, mountroot, fusepath, NUMELEM(pathbuf));
    if (fullpath == NULL) {
        KFS_RETURN(-errno);
    }
    /* This could also use gnulib.utimens, but gnulib is too big for now. */
    tvmicro[0].tv_sec = tvnano[0].tv_sec;
    tvmicro[0].tv_usec = tvnano[0].tv_nsec / 1000;
    tvmicro[1].tv_sec = tvnano[1].tv_sec;
    tvmicro[1].tv_usec = tvnano[1].tv_nsec / 1000;
    ret = utimes(fullpath, tvmicro);
    if (ret == -1) {
        ret = -errno;
    }
    fullpath = KFS_BUFSTRFREE(fullpath, pathbuf);

    KFS_RETURN(ret);
}

static const struct kfs_operations posix_oper = {
    .getattr = posix_getattr,
    .readlink = posix_readlink,
    .mknod = posix_mknod,
    .mkdir = posix_mkdir,
    .unlink = posix_unlink,
    .rmdir = posix_rmdir,
    .symlink = posix_symlink,
    .rename = posix_rename,
    .link = posix_link,
    .chmod = posix_chmod,
    .chown = posix_chown,
    .truncate = posix_truncate,
    .open = posix_open,
    .read = posix_read,
    .write = posix_write,
    .statfs = posix_statfs,
    .flush = posix_flush,
    .release = posix_release,
    .fsync = posix_fsync,
    .setxattr = posix_setxattr,
    .getxattr = posix_getxattr,
    .listxattr = posix_listxattr,
    .removexattr = posix_removexattr,
    .opendir = posix_opendir,
    .readdir = posix_readdir,
    .releasedir = posix_releasedir,
    .fsyncdir = nosys_fsyncdir,
    .access = posix_access,
    .create = posix_create,
    .ftruncate = posix_ftruncate,
    .fgetattr = posix_fgetattr,
    .lock = nosys_lock,
    .utimens = posix_utimens,
    .bmap = nosys_bmap,
#if FUSE_VERSION >= 28
    .ioctl = nosys_ioctl,
    .poll = nosys_poll,
#endif
};

/**
 * Global initialization.
 */
static void *
kenny_init(const char *conffile, const char *section, size_t num_subvolumes,
        const struct kfs_subvolume subvolumes[])
{
    (void) subvolumes;

    char *mountroot = NULL;

    KFS_ENTER();

    KFS_ASSERT(section != NULL && conffile != NULL);
    if (num_subvolumes != 0) {
        KFS_ERROR("Brick `%s' (POSIX) takes no subvolumes.", section);
        KFS_RETURN(NULL);
    }
    mountroot = kfs_ini_gets(conffile, section, "path");
    if (mountroot == NULL) {
        KFS_ERROR("Missing value `path' in section [%s] of file %s.", section,
                conffile);
        KFS_RETURN(NULL);
    }
    KFS_INFO("Started POSIX brick `%s': mirroring `%s'.", section, mountroot);

    KFS_RETURN(mountroot);
}

/*
 * Get the backend interface.
 */
static const struct kfs_operations *
kenny_getfuncs(void)
{
    KFS_ENTER();

    KFS_RETURN(&posix_oper);
}

/**
 * Global cleanup.
 */
static void
kenny_halt(void *private_data)
{
    KFS_ENTER();

    private_data = KFS_FREE(private_data);

    KFS_RETURN();
}

static const struct kfs_brick_api kenny_api = {
    .init = kenny_init,
    .getfuncs = kenny_getfuncs,
    .halt = kenny_halt,
};

/*
 * Public functions.
 */

/**
 * Get the KennyFS API: initialiser, FUSE API getter and cleaner. This function
 * has the generic KennyFS backend name expected by frontends. Do not put this
 * in the header-file but extract it with dynamic linking.
 */
const struct kfs_brick_api *
kfs_brick_getapi(void)
{
    KFS_ENTER();

    KFS_RETURN(&kenny_api);
}
