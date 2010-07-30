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

#include "kfs_brick_posix.h"

/* <attr/xattr.h> needs this header. */
#include <sys/types.h>

#ifdef KFS_USE_XATTR
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

#include "kfs.h"
#include "kfs_api.h"
#include "kfs_misc.h"

/**
 * Free given string buffer if it does not equal given static buffer. Useful for
 * cleaning up potential allocations by kfs_bufstrcat(). Returns the strbuf,
 * either free'd or untouched (just don't use it after this).
 */
#define KFS_BUFSTRFREE(strbuf, staticbuf) (((strbuf) == (staticbuf)) \
                                                    ? (strbuf) \
                                                    : KFS_FREE(strbuf))

/*
 * Types.
 */

struct kfs_brick_posix_arg {
    char *path;
};


/*
 * Globals and statics.
 */

static struct kfs_brick_posix_arg *myconf;

/*
 * FUSE API.
 */

static int
kenny_getattr(const char *fusepath, struct stat *stbuf)
{
    int ret = 0;
    /* On-stack buffer for paths of limited length. Otherwise: malloc(). */
    char pathbuf[PATHBUF_SIZE];
    char *fullpath = NULL;

    KFS_ENTER();

    fullpath = kfs_bufstrcat(pathbuf, myconf->path, fusepath, NUMELEM(pathbuf));
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
kenny_fgetattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
    (void) path;

    int ret = 0;

    KFS_ENTER();

    ret = fstat(fi->fh, stbuf);
    if (ret == -1) {
        ret = -errno;
    }

    KFS_RETURN(ret);
}

/**
 * Read the target of a symlink.
 */
static int
kenny_readlink(const char *fusepath, char *buf, size_t size)
{
    char pathbuf[PATHBUF_SIZE];
    ssize_t ret = 0;
    char *fullpath = NULL;

    KFS_ENTER();

    ret = 0;
    fullpath = kfs_bufstrcat(pathbuf, myconf->path, fusepath, NUMELEM(pathbuf));
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
kenny_mknod(const char *fusepath, mode_t mode, dev_t dev)
{
    char pathbuf[PATHBUF_SIZE];
    int ret = 0;
    char *fullpath = NULL;

    KFS_ENTER();

    fullpath = kfs_bufstrcat(pathbuf, myconf->path, fusepath, NUMELEM(pathbuf));
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
kenny_truncate(const char *fusepath, off_t offset)
{
    char pathbuf[PATHBUF_SIZE];
    int ret = 0;
    char *fullpath = NULL;

    KFS_ENTER();

    fullpath = kfs_bufstrcat(pathbuf, myconf->path, fusepath, NUMELEM(pathbuf));
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
kenny_open(const char *fusepath, struct fuse_file_info *fi)
{
    char pathbuf[PATHBUF_SIZE];
    char *fullpath = NULL;
    int ret = 0;

    KFS_ENTER();

    ret = 0;
    fullpath = kfs_bufstrcat(pathbuf, myconf->path, fusepath, NUMELEM(pathbuf));
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
kenny_unlink(const char *fusepath)
{
    char pathbuf[PATHBUF_SIZE];
    char *fullpath = NULL;
    int ret = 0;

    KFS_ENTER();

    ret = 0;
    fullpath = kfs_bufstrcat(pathbuf, myconf->path, fusepath, NUMELEM(pathbuf));
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
kenny_rmdir(const char *fusepath)
{
    char pathbuf[PATHBUF_SIZE];
    char *fullpath = NULL;
    int ret;

    KFS_ENTER();

    fullpath = kfs_bufstrcat(pathbuf, myconf->path, fusepath, NUMELEM(pathbuf));
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
kenny_symlink(const char *path1, const char *path2)
{
    char pathbuf[PATHBUF_SIZE];
    char *fullpath = NULL;
    int ret = 0;

    KFS_ENTER();

    fullpath = kfs_bufstrcat(pathbuf, myconf->path, path2, NUMELEM(pathbuf));
    ret = symlink(path1, fullpath);
    if (ret == -1) {
        ret = -errno;
    }
    fullpath = KFS_BUFSTRFREE(fullpath, pathbuf);

    KFS_RETURN(ret);
}

static int
kenny_rename(const char *from, const char *to)
{
    char pathbuf_from[PATHBUF_SIZE];
    char pathbuf_to[PATHBUF_SIZE];
    char *fullpath_from = NULL;
    char *fullpath_to = NULL;
    int ret;

    KFS_ENTER();

    fullpath_from = kfs_bufstrcat(pathbuf_from, myconf->path, from,
            NUMELEM(pathbuf_from));
    fullpath_to = kfs_bufstrcat(pathbuf_to, myconf->path, to,
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
kenny_link(const char *from, const char *to)
{
    char pathbuf_from[PATHBUF_SIZE];
    char pathbuf_to[PATHBUF_SIZE];
    char *fullpath_from = NULL;
    char *fullpath_to = NULL;
    int ret;

    KFS_ENTER();

    fullpath_from = kfs_bufstrcat(pathbuf_from, myconf->path, from,
            NUMELEM(pathbuf_from));
    fullpath_to = kfs_bufstrcat(pathbuf_to, myconf->path, to,
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
kenny_chmod(const char *fusepath, mode_t mode)
{
    char pathbuf[PATHBUF_SIZE];
    char *fullpath = NULL;
    int ret;

    KFS_ENTER();

    fullpath = kfs_bufstrcat(pathbuf, myconf->path, fusepath, NUMELEM(pathbuf));
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
kenny_chown(const char *fusepath, uid_t uid, gid_t gid)
{
    char pathbuf[PATHBUF_SIZE];
    char *fullpath = NULL;
    int ret;

    KFS_ENTER();

    fullpath = kfs_bufstrcat(pathbuf, myconf->path, fusepath, NUMELEM(pathbuf));
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
kenny_read(const char *fusepath, char *buf, size_t size, off_t offset, struct
        fuse_file_info *fi)
{
    (void) fusepath;

    int ret = 0;

    KFS_ENTER();

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
kenny_write(const char *fusepath, const char *buf, size_t size, off_t offset,
        struct fuse_file_info *fi)
{
    (void) fusepath;

    int ret = 0;

    KFS_ENTER();

    ret = pwrite(fi->fh, buf, size, offset);
    if (ret == -1) {
        KFS_RETURN(-errno);
    }

    KFS_RETURN(0);
}

#ifdef KFS_USE_XATTR
/*
 * Extended attributes.
 */

static int
kenny_setxattr(const char *fusepath, const char *name, const char *value, size_t
        size, int flags)
{
    char pathbuf[PATHBUF_SIZE];
    int ret = 0;
    char *fullpath = NULL;

    KFS_ENTER();

    ret = 0;
    fullpath = kfs_bufstrcat(pathbuf, myconf->path, fusepath, NUMELEM(pathbuf));
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
kenny_getxattr(const char *fusepath, const char *name, char *value, size_t size)
{
    char pathbuf[PATHBUF_SIZE];
    int ret = 0;
    char *fullpath = NULL;

    KFS_ENTER();

    fullpath = kfs_bufstrcat(pathbuf, myconf->path, fusepath, NUMELEM(pathbuf));
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
kenny_listxattr(const char *fusepath, char *list, size_t size)
{
    char pathbuf[PATHBUF_SIZE];
    ssize_t ret = 0;
    char *fullpath = NULL;

    KFS_ENTER();

    fullpath = kfs_bufstrcat(pathbuf, myconf->path, fusepath, NUMELEM(pathbuf));
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
kenny_removexattr(const char *fusepath, const char *name)
{
    char pathbuf[PATHBUF_SIZE];
    int ret = 0;
    char *fullpath = NULL;

    KFS_ENTER();

    fullpath = kfs_bufstrcat(pathbuf, myconf->path, fusepath, NUMELEM(pathbuf));
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
#endif

/*
 * Directories.
 */

static int
kenny_mkdir(const char *fusepath, mode_t mode)
{
    char pathbuf[PATHBUF_SIZE];
    int ret = 0;
    char *fullpath = NULL;

    KFS_ENTER();

    fullpath = kfs_bufstrcat(pathbuf, myconf->path, fusepath, NUMELEM(pathbuf));
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
kenny_opendir(const char *fusepath, struct fuse_file_info *fi)
{
    char pathbuf[PATHBUF_SIZE];
    char *fullpath = NULL;
    DIR *dir = NULL;
    int ret = 0;

    KFS_ENTER();

    fullpath = kfs_bufstrcat(pathbuf, myconf->path, fusepath, NUMELEM(pathbuf));
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
kenny_readdir(const char *fusepath, void *buf, fuse_fill_dir_t filler,
        off_t offset, struct fuse_file_info *fi)
{
    (void) fusepath;

    struct stat stbuf;
    struct stat *stbufp = NULL;
    DIR *dir = NULL;
    struct dirent *de = NULL;
    int ret = 0;

    KFS_ENTER();

    memcpy(&dir, &fi->fh, sizeof(dir));
    seekdir(dir, offset);
    for (;;) {
        /* Get an entry. */
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
kenny_releasedir(const char *fusepath, struct fuse_file_info *fi)
{
    (void) fusepath;

    DIR *dir = NULL;
    int ret = 0;

    KFS_ENTER();

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
kenny_utimens(const char *fusepath, const struct timespec tvnano[2])
{
    char pathbuf[PATHBUF_SIZE];
    struct timeval tvmicro[2];
    int ret = 0;
    char *fullpath = NULL;

    KFS_ENTER();

    ret = 0;
    fullpath = kfs_bufstrcat(pathbuf, myconf->path, fusepath, NUMELEM(pathbuf));
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

static const struct fuse_operations kenny_oper = {
    .getattr = kenny_getattr,
    .readlink = kenny_readlink,
    .mknod = kenny_mknod,
    .mkdir = kenny_mkdir,
    .unlink = kenny_unlink,
    .rmdir = kenny_rmdir,
    .symlink = kenny_symlink,
    .rename = kenny_rename,
    .link = kenny_link,
    .chmod = kenny_chmod,
    .chown = kenny_chown,
    .truncate = kenny_truncate,
    .open = kenny_open,
    .read = kenny_read,
    .write = kenny_write,
    .statfs = NULL,
    .flush = NULL,
    .release = NULL,
    .fsync = NULL,
#if KFS_USE_XATTR
    .setxattr = kenny_setxattr,
    .getxattr = kenny_getxattr,
    .listxattr = kenny_listxattr,
    .removexattr = kenny_removexattr,
#endif
    .opendir = kenny_opendir,
    .readdir = kenny_readdir,
    .releasedir = kenny_releasedir,
    .fsyncdir = NULL,
    .init = NULL,
    .destroy = NULL,
    .access = NULL,
    .create = NULL,
    .ftruncate = NULL,
    .fgetattr = kenny_fgetattr,
    .lock = NULL,
    .utimens = kenny_utimens,
};

/**
 * Create a new arg struct, specific for the POSIX brick.
 * TODO: could use a better name.
 */
static struct kfs_brick_posix_arg *
private_makearg(char *path)
{
    struct kfs_brick_posix_arg *arg = NULL;

    KFS_ENTER();

    KFS_ASSERT(path != NULL);
    arg = KFS_MALLOC(sizeof(*arg));
    if (arg == NULL) {
        KFS_ERROR("malloc: %s", strerror(errno));
    } else {
        arg->path = kfs_strcpy(path);
        if (arg->path == NULL) {
            arg = KFS_FREE(arg);
            arg = NULL;
        }
    }

    KFS_RETURN(arg);
}

/**
 * Free a POSIX brick arg struct.
 */
static struct kfs_brick_posix_arg *
private_delarg(struct kfs_brick_posix_arg *arg)
{
    KFS_ENTER();

    KFS_ASSERT(arg != NULL && arg->path != NULL);
    arg->path = KFS_FREE(arg->path);
    arg = KFS_FREE(arg);

    KFS_RETURN(arg);
}

/**
 * Unserialize an argument.
 */
static struct kfs_brick_posix_arg *
kfs_brick_posix_char2arg(char *buf, size_t len)
{
    KFS_NASSERT((void) len);

    struct kfs_brick_posix_arg *arg = NULL;

    KFS_ENTER();

    KFS_ASSERT(buf != NULL && len > 0);
    KFS_ASSERT(buf[len - 1] == '\0');
    arg = private_makearg(buf);

    KFS_RETURN(arg);
}

/**
 * Serialize an argument to a character array. Returns the size of the new
 * buffer, which must, eventually, be freed. Returns the size of the buffer on
 * success, sets the buffer to NULL on failure.
 */
static size_t
kfs_brick_posix_arg2char(char **buf, const struct kfs_brick_posix_arg *arg)
{
    ssize_t len = 0;

    KFS_ENTER();

    KFS_ASSERT(arg != NULL && buf != NULL);
    len = strlen(arg->path) + 1; /* +1 for the additional '\0' byte. */
    *buf = KFS_MALLOC(len);
    if (*buf != NULL) {
        *buf = memcpy(*buf, arg->path, len);
    }

    KFS_RETURN(len);
}

/**
 * Initialize a new argument struct. Returns NULL on error, pointer to struct on
 * success. That pointer must eventually be freed. This function is useful for
 * other bricks / frontends to create arguments compatible with this brick.
 */
static struct kfs_brick_arg *
kenny_makearg(char *path)
{
    size_t serial_size = 0;
    char *serial_buf = NULL;
    struct kfs_brick_posix_arg *arg_specific = NULL;
    struct kfs_brick_arg *arg_generic = NULL;

    KFS_ENTER();

    KFS_ASSERT(path != NULL);
    arg_generic = NULL;
    /* Create a posix block-specific argument. */
    arg_specific = private_makearg(path);
    if (arg_specific != NULL) {
        /* Transform that into a generic arg (serialization). */
        serial_size = kfs_brick_posix_arg2char(&serial_buf, arg_specific);
        if (serial_buf != NULL) {
            /*
             * Wrap serialized argument into generic struct. Error checking
             * happens later.
             */
            arg_generic = kfs_brick_makearg(serial_buf, serial_size);
            if (arg_generic == NULL) {
                serial_buf = KFS_FREE(serial_buf);
            }
        }
        private_delarg(arg_specific);
    }

    KFS_RETURN(arg_generic);
}

/**
 * Global initialization.
 */
static int
kenny_init(struct kfs_brick_arg *generic)
{
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(generic != NULL);
    KFS_ASSERT(generic->payload != NULL && generic->payload_size > 0);
    KFS_ASSERT(generic->num_next_bricks == 0);
    myconf = kfs_brick_posix_char2arg(generic->payload, generic->payload_size);
    if (myconf != NULL) {
        ret = 0;
    } else {
        KFS_ERROR("Initializing POSIX brick failed.");
        ret = -1;
    }

    KFS_RETURN(ret);
}

/*
 * Get the backend interface.
 */
static const struct fuse_operations *
kenny_getfuncs(void)
{
    KFS_ENTER();

    KFS_RETURN(&kenny_oper);
}

/**
 * Global cleanup.
 */
static void
kenny_halt(void)
{
    KFS_ENTER();

    myconf = private_delarg(myconf);

    KFS_RETURN();
}

static const struct kfs_brick_api kenny_api = {
    .makearg = kenny_makearg,
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
