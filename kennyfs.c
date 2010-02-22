#define KENNYFS_VERSION "0.0"

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
#include <fuse_opt.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <utime.h>

#include "kennyfs.h"
#include "kfs_misc.h"

#define KENNYFS_OPT(t, p, v) {t, offsetof(struct kenny_conf, p), v}
#define PATHBUF_SIZE 256

enum {
    KEY_HELP,
    KEY_VERSION,
};

struct kenny_conf {
    char *path;
};

struct kenny_fh {
    DIR *dir;
    int fd;
};

static struct kenny_conf myconf;
static struct fuse_opt kenny_opts[] = {
    KENNYFS_OPT("path=%s", path, 0),
    FUSE_OPT_KEY("-h", KEY_HELP),
    FUSE_OPT_KEY("--help", KEY_HELP),
    FUSE_OPT_KEY("-v", KEY_VERSION),
    FUSE_OPT_KEY("--version", KEY_VERSION),
    FUSE_OPT_END
};


/**
 * ***** Private functions. *****
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
 * FUSE API
 */

/**
 * Mirrors all calls to the source dir.
 */
static int
kenny_getattr(const char *fusepath, struct stat *stbuf)
{
    int ret = 0;
    /* On-stack buffer for paths of limited length. Otherwise: malloc(). */
    char pathbuf[PATHBUF_SIZE];
    char *fullpath = NULL;

    fullpath = kfs_bufstrcat(pathbuf, myconf.path, fusepath, AR_SIZE(pathbuf));
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
    fullpath = kfs_bufstrcat(pathbuf, myconf.path, fusepath, AR_SIZE(pathbuf));
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
    fullpath = kfs_bufstrcat(pathbuf, myconf.path, fusepath, AR_SIZE(pathbuf));
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
            KFS_FREE(fullpath);
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
    fullpath = kfs_bufstrcat(pathbuf, myconf.path, fusepath, AR_SIZE(pathbuf));
    if (fullpath == NULL) {
        ret = -errno;
    } else {
        ret = setxattr(fullpath, name, value, size, flags);
        if (ret == -1) {
            KFS_ERROR("setxattr: %s", strerror(errno));
            ret = -errno;
        }
        if (fullpath != pathbuf) {
            KFS_FREE(fullpath);
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
    fullpath = kfs_bufstrcat(pathbuf, myconf.path, fusepath, AR_SIZE(pathbuf));
    if (fullpath == NULL) {
        ret = -errno;
    } else {
        ret = getxattr(fullpath, name, value, size);
        if (ret == -1) {
            KFS_ERROR("getxattr: %s", strerror(errno));
            ret = -errno;
        }
        if (fullpath != pathbuf) {
            KFS_FREE(fullpath);
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
    fullpath = kfs_bufstrcat(pathbuf, myconf.path, fusepath, AR_SIZE(pathbuf));
    if (fullpath == NULL) {
        ret = -errno;
    } else {
        ret = listxattr(fullpath, list, size);
        if (ret == -1) {
            KFS_ERROR("listxattr: %s", strerror(errno));
            ret = -errno;
        }
        if (fullpath != pathbuf) {
            KFS_FREE(fullpath);
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
    fullpath = kfs_bufstrcat(pathbuf, myconf.path, fusepath, AR_SIZE(pathbuf));
    if (fullpath == NULL) {
        ret = -errno;
    } else {
        ret = removexattr(fullpath, name);
        if (ret == -1) {
            KFS_ERROR("removexattr: %s", strerror(errno));
            ret = -errno;
        }
        if (fullpath != pathbuf) {
            KFS_FREE(fullpath);
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
    fullpath = kfs_bufstrcat(pathbuf, myconf.path, fusepath, AR_SIZE(pathbuf));
    if (fh == NULL || fullpath == NULL) {
        KFS_ERROR("malloc: %s", strerror(errno));
        ret = -errno;
    } else {
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
    fullpath = kfs_bufstrcat(pathbuf, myconf.path, fusepath, AR_SIZE(pathbuf));
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
            KFS_FREE(fullpath);
        }
    }

    return ret;
}


static struct fuse_operations kenny_oper = {
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
 * Process command line arguments.
 */
static int
kenny_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs)
{
    (void) data;
    (void) arg;

    switch(key) {
    case KEY_VERSION:
        fprintf(stderr, "KennyFS version %s\n", KENNYFS_VERSION);
        fuse_opt_add_arg(outargs, "--version");
        fuse_main(outargs->argc, outargs->argv, &kenny_oper, NULL);
        exit(EXIT_SUCCESS);

    case KEY_HELP:
        fprintf(stderr,
                "usage: %s mountpoint [options]\n"
                "\n"
                "general options:\n"
                "    -o opt,[opt...]  mount options\n"
                "    -h   --help      print help\n"
                "    -V   --version   print version\n"
                "\n"
                "KennyFS options:\n"
                "    -o path=PATH     path to mirror\n"
                "\n",
                outargs->argv[0]);
        fuse_opt_add_arg(outargs, "-ho");
        fuse_main(outargs->argc, outargs->argv, &kenny_oper, NULL);
        exit(EXIT_SUCCESS);
    }

    return 1;
}


/***** Public functions. *****/

int
main(int argc, char *argv[])
{
    int ret = 0;
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    KFS_INFO("Starting KennyFS version %s.", KENNYFS_VERSION);
    memset(&myconf, 0, sizeof(myconf));
    ret = fuse_opt_parse(&args, &myconf, kenny_opts, kenny_opt_proc);
    if (ret == -1 || myconf.path == NULL) {
        return EXIT_FAILURE;
    }
    ret = fuse_main(args.argc, args.argv, &kenny_oper, NULL);
    fuse_opt_free_args(&args);
    if (ret != 0) {
        KFS_WARNING("KennyFS exited with value %d.", ret);
    } else {
        KFS_INFO("KennyFS exited succesfully.");
    }

    return ret;
}
