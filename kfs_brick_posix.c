/**
 * KennyFS backend forwarding everything to a locally mounted POSIX-compliant
 * directory.
 */

#define FUSE_USE_VERSION 26
/* Macro is necessary to get fstatat(). */
#define _ATFILE_SOURCE
/* Macro is necessary to get pread(). */
#define _XOPEN_SOURCE 500
/* Macro is necessary to get dirfd(). */
#define _BSD_SOURCE
/* <attr/xattr.h> needs this header. */
#include <sys/types.h>

#include <attr/xattr.h>
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
#include "kfs_brick_posix.h"
#include "kfs_misc.h"

/* Many functions use this macro in allocating a buffer on the stack to build a
 * full pathname. When it is exceeded more memory is automatically allocated on
 * the heap. Happens with kfs_bufstrcat().
 */
#define PATHBUF_SIZE 256

/*
 * Types.
 */

struct kenny_fh {
    DIR *dir;
    int fd;
};

struct kfs_brick_posix_arg {
    char *path;
};


/*
 * Globals and statics.
 */

static struct kfs_brick_posix_arg *myconf;

/*
 * Private functions.
 */

/**
 * Convert FUSE fuse_file_info::fh to struct kenny_fh (typecast). I might be
 * wrong, but I believe a simple type cast could introduce errors on
 * architectures where uint64_t and pointers to (my) structs are aligned
 * differently. Memcpy does not suffer from this limitation.
 */
static struct kenny_fh *
fh_fuse2kenny(uint64_t fh)
{
    struct kenny_fh *kfh = NULL;

    memcpy(&kfh, &fh, min(sizeof(kfh), sizeof(fh)));

    return kfh;
}

/**
 * Convert struct kenny_fh to fuse_file_info::fh (typecast). See
 * fh_fuse2kenny().
 */
static uint64_t
fh_kenny2fuse(struct kenny_fh *fh)
{
    uint64_t fusefh = 0;

    memcpy(&fusefh, &fh, min(sizeof(fusefh), sizeof(fh)));

    return fusefh;
}

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

    fullpath = kfs_bufstrcat(pathbuf, myconf->path, fusepath, NUMELEM(pathbuf));
    if (fullpath == NULL) {
        ret = -errno;
    } else {
        KFS_DEBUG("Getattr on %s", fullpath);
        ret = lstat(fullpath, stbuf);
        if (ret == -1) {
            KFS_ERROR("stat: %s", strerror(errno));
            ret = -errno;
        }
        if (fullpath != pathbuf) {
            fullpath = KFS_FREE(fullpath);
        }
    }

    return ret;
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

    ret = 0;
    fullpath = kfs_bufstrcat(pathbuf, myconf->path, fusepath, NUMELEM(pathbuf));
    if (fullpath == NULL) {
        ret = -errno;
    } else {
        /* Save room for the \0 byte. */
        ret = readlink(fullpath, buf, size - 1);
        if (ret == -1) {
            KFS_ERROR("readlink: %s", strerror(errno));
            ret = -errno;
        } else {
            buf[ret] = '\0';
            ret = 0;
        }
        if (fullpath != pathbuf) {
            fullpath = KFS_FREE(fullpath);
        }
    }

    return ret;
}

static int
kenny_open(const char *fusepath, struct fuse_file_info *fi)
{
    char pathbuf[PATHBUF_SIZE];
    char *fullpath = NULL;
    int ret = 0;

    ret = 0;
    fullpath = kfs_bufstrcat(pathbuf, myconf->path, fusepath, NUMELEM(pathbuf));
    if (fullpath == NULL) {
        ret = -errno;
    } else {
        ret = open(fullpath, fi->flags);
        if (ret == -1) {
            KFS_ERROR("open: %s", strerror(errno));
            ret = -errno;
        } else {
            /* Store file descriptor in FUSE file handle. */
            fi->fh = ret;
            ret = 0;
        }
        if (fullpath != pathbuf) {
            fullpath = KFS_FREE(fullpath);
        }
    }

    return ret;
}

/**
 * Read the contents of given file.
 * TODO: Check compliance of return value with API.
 */
static int
kenny_read(const char *fusepath, char *buf, size_t size, off_t offset, struct
        fuse_file_info *fi)
{
    (void) fusepath;

    int ret = 0;
    ssize_t numread = 0;

    ret = 0;
    numread = pread(fi->fh, buf, size, offset);
    if (numread == -1) {
        KFS_ERROR("pread: %s", strerror(errno));
        ret = -errno;
    } else {
        ret = numread;
    }

    return ret;
}

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

    ret = 0;
    fullpath = kfs_bufstrcat(pathbuf, myconf->path, fusepath, NUMELEM(pathbuf));
    if (fullpath == NULL) {
        ret = -errno;
    } else {
        ret = lsetxattr(fullpath, name, value, size, flags);
        if (ret == -1) {
            KFS_ERROR("setxattr: %s", strerror(errno));
            ret = -errno;
        }
        if (fullpath != pathbuf) {
            fullpath = KFS_FREE(fullpath);
        }
    }

    return ret;
}

static int
kenny_getxattr(const char *fusepath, const char *name, char *value, size_t size)
{
    char pathbuf[PATHBUF_SIZE];
    int ret = 0;
    char *fullpath = NULL;

    ret = 0;
    fullpath = kfs_bufstrcat(pathbuf, myconf->path, fusepath, NUMELEM(pathbuf));
    if (fullpath == NULL) {
        ret = -errno;
    } else {
        ret = lgetxattr(fullpath, name, value, size);
        if (ret == -1) {
            KFS_ERROR("getxattr: %s", strerror(errno));
            ret = -errno;
        }
        if (fullpath != pathbuf) {
            fullpath = KFS_FREE(fullpath);
        }
    }

    return ret;
}

static int
kenny_listxattr(const char *fusepath, char *list, size_t size)
{
    char pathbuf[PATHBUF_SIZE];
    ssize_t ret = 0;
    char *fullpath = NULL;

    ret = 0;
    fullpath = kfs_bufstrcat(pathbuf, myconf->path, fusepath, NUMELEM(pathbuf));
    if (fullpath == NULL) {
        ret = -errno;
    } else {
        ret = llistxattr(fullpath, list, size);
        if (ret == -1) {
            KFS_ERROR("listxattr: %s", strerror(errno));
            ret = -errno;
        }
        if (fullpath != pathbuf) {
            fullpath = KFS_FREE(fullpath);
        }
    }

    return ret;
}

static int
kenny_removexattr(const char *fusepath, const char *name)
{
    char pathbuf[PATHBUF_SIZE];
    int ret = 0;
    char *fullpath = NULL;

    ret = 0;
    fullpath = kfs_bufstrcat(pathbuf, myconf->path, fusepath, NUMELEM(pathbuf));
    if (fullpath == NULL) {
        ret = -errno;
    } else {
        ret = lremovexattr(fullpath, name);
        if (ret == -1) {
            KFS_ERROR("removexattr: %s", strerror(errno));
            ret = -errno;
        }
        if (fullpath != pathbuf) {
            fullpath = KFS_FREE(fullpath);
        }
    }

    return ret;
}

/*
 * Directories.
 */

static int
kenny_opendir(const char *fusepath, struct fuse_file_info *fi)
{
    struct kenny_fh *fh = NULL;
    int ret = 0;
    char pathbuf[PATHBUF_SIZE];
    char *fullpath = NULL;

    ret = 0;
    fh = KFS_MALLOC(sizeof(*fh));
    if (fh == NULL || fullpath == NULL) {
        KFS_ERROR("malloc: %s", strerror(errno));
        ret = -errno;
    } else {
        fullpath = kfs_bufstrcat(pathbuf, myconf->path, fusepath,
                NUMELEM(pathbuf));
        KFS_DEBUG("Opening directory %s.", fullpath);
        fi->fh = fh_kenny2fuse(fh);
        fh->dir = opendir(fullpath);
        if (fh->dir == NULL) {
            KFS_ERROR("opendir: %s", strerror(errno));
            ret = -errno;
        }
        if (fullpath != pathbuf) {
            fullpath = KFS_FREE(fullpath);
        }
    }

    return ret;
}

/**
 * List directory contents.
 */
static int
kenny_readdir(const char *fusepath, void *buf, fuse_fill_dir_t filler,
        off_t offset, struct fuse_file_info *fi)
{
    (void) offset;

    struct stat mystat;
    struct kenny_fh *fh = NULL;
    struct dirent *mydirent = NULL;
    int read_more = 0;
    int ret = 0;

    KFS_DEBUG("Reading elements from dir %s", fusepath);
    fh = fh_fuse2kenny(fi->fh);
    read_more = 1;
    while (read_more) {
        errno = 0;
        /* Get an entry. */
        mydirent = readdir(fh->dir);
        if (mydirent == NULL) {
            if (errno != 0) {
                KFS_ERROR("readdir: %s", strerror(errno));
                ret = -errno;
            }
            read_more = 0;
            break;
        }
        /* Stat that entry. This is not (yet) POSIX compliant. */
        ret = fstatat(dirfd(fh->dir), mydirent->d_name, &mystat, 0);
        if (ret == -1) {
            KFS_ERROR("fstatat: %s", strerror(errno));
            return -errno;
        }
        /* Add it to the return-buffer. */
        ret = filler(buf, mydirent->d_name, &mystat, 0);
        if (ret == 1) {
            return -ENOBUFS;
        }
    }
    return ret;
}

static int
kenny_releasedir(const char *fusepath, struct fuse_file_info *fi)
{
    (void) fusepath;

    struct kenny_fh *fh = NULL;
    int ret = 0;

    fh = fh_fuse2kenny(fi->fh);
    ret = closedir(fh->dir);
    if (ret == -1) {
        KFS_ERROR("closedir: %s", strerror(errno));
        return -errno;
    }
    fh->dir = NULL;
    fh = KFS_FREE(fh);
    fi->fh = 0;

    return 0;
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

    ret = 0;
    fullpath = kfs_bufstrcat(pathbuf, myconf->path, fusepath, NUMELEM(pathbuf));
    if (fullpath == NULL) {
        ret = -errno;
    } else {
        /* This could also use gnulib.utimens, but gnulib is too big for now. */
        tvmicro[0].tv_sec = tvnano[0].tv_sec;
        tvmicro[0].tv_usec = tvnano[0].tv_nsec / 1000;
        tvmicro[1].tv_sec = tvnano[1].tv_sec;
        tvmicro[1].tv_usec = tvnano[1].tv_nsec / 1000;
        ret = utimes(fullpath, tvmicro);
        if (ret == -1) {
            KFS_ERROR("removexattr: %s", strerror(errno));
            ret = -errno;
        }
        if (fullpath != pathbuf) {
            fullpath = KFS_FREE(fullpath);
        }
    }

    return ret;
}

static const struct fuse_operations kenny_oper = {
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

/**
 * Create a new arg struct, specific for the POSIX brick.
 * TODO: could use a better name.
 */
static struct kfs_brick_posix_arg *
private_makearg(char *path)
{
    struct kfs_brick_posix_arg *arg = NULL;

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

    return arg;
}

/**
 * Free a POSIX brick arg struct.
 */
static struct kfs_brick_posix_arg *
private_delarg(struct kfs_brick_posix_arg *arg)
{
    KFS_ASSERT(arg != NULL && arg->path != NULL);
    arg->path = KFS_FREE(arg->path);
    arg = KFS_FREE(arg);

    return arg;
}

/**
 * Unserialize an argument.
 */
static struct kfs_brick_posix_arg *
kfs_brick_posix_char2arg(char *buf, size_t len)
{
    struct kfs_brick_posix_arg *arg = NULL;

    KFS_ASSERT(buf != NULL);
    KFS_ASSERT(buf[len - 1] == '\0');
    arg = private_makearg(buf);

    return arg;
}

/**
 * Serialize an argument to a character array. Returns the size of the new
 * buffer, which must, eventually, be freed. Returns -1 on error.
 */
static ssize_t
kfs_brick_posix_arg2char(char **buf, struct kfs_brick_posix_arg *arg)
{
    ssize_t len = 0; 
    char *serialized = NULL;

    KFS_ASSERT(arg != NULL && buf != NULL);
    *buf = kfs_strcpy(arg->path);
    serialized = KFS_MALLOC(len);
    if (serialized == NULL) {
        len = -1;
    } else {
        len = strlen(arg->path) + 1; /* The additional '\0' byte. */
    }

    return len;
}

/**
 * Initialize a new argument struct. Returns NULL on error, pointer to struct on
 * success. That pointer must eventually be freed. This function is useful for
 * other bricks / frontends to create arguments compatible with this brick.
 */
static struct kfs_brick_arg *
kenny_makearg(char *path)
{
    ssize_t serial_size = 0;
    char *serial_buf = NULL;
    struct kfs_brick_posix_arg *arg_specific = NULL;
    struct kfs_brick_arg *arg_generic = NULL;

    KFS_ASSERT(path != NULL);
    arg_generic = NULL;
    /* Create a posix block-specific argument. */
    arg_specific = private_makearg(path);
    if (arg_specific != NULL) {
        /* Transform that into a generic arg (serialization). */
        serial_size = kfs_brick_posix_arg2char(&serial_buf, arg_specific);
        if (serial_size != -1) {
            arg_generic = KFS_MALLOC(sizeof(*arg_generic));
            if (serial_size == -1) {
                /*
                 * Wrap serialized argument into generic struct. Error checking
                 * happens later.
                 */
                arg_generic = kfs_brick_makearg(serial_buf, serial_size);
            }
        }
        private_delarg(arg_specific);
    }
    if (serial_size != -1) {
        /* Everything went right: construct the generic arg. */
        arg_generic->payload_size = serial_size;
        arg_generic->payload = serial_buf;
        arg_generic->num_next_bricks = 0;
        arg_generic->next_bricks = NULL;
        arg_generic->next_args = NULL;
    }

    return arg_generic;
}

/**
 * Global initialization.
 */
static int
kenny_init(struct kfs_brick_arg *generic)
{
    int ret = 0;

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

    return ret;
}

/*
 * Get the backend interface.
 */
static const struct fuse_operations *
kenny_getfuncs(void)
{
    return &kenny_oper;
}

/**
 * Global cleanup.
 */
static void
kenny_halt(void)
{
    myconf = private_delarg(myconf);

    return;
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
struct kfs_brick_api
kfs_brick_getapi(void)
{
    return kenny_api;
}
