#define KENNYFS_VERSION "0.0"
#define FUSE_USE_VERSION 26

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
#include <sys/types.h>
#include <unistd.h>

#include "kennyfs.h"

#define KENNYFS_OPT(t, p, v) {t, offsetof(struct kenny_conf, p), v}

enum {
    KEY_HELP,
    KEY_VERSION,
};

struct kenny_conf {
    char *path;
};

struct kenny_fh {
    DIR *dir;
};

static const char *kenny_path = "/hello";
static char *kenny_str = NULL;
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

static int
min(int x, int y) {
    return x > y ? y : x;
}

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

/**
 * Mirrors all calls to the source dir.
 */
static int
kenny_getattr(const char *fusepath, struct stat *stbuf)
{
    int ret = 0;
    int must_malloc = 0;
    /* On-stack buffer for paths of limited length. Otherwise: malloc(). */
    char pathbuf[512];
    char *mypath = pathbuf;

    memset(stbuf, 0, sizeof(*stbuf));
    /* The buffer is too small. */
    must_malloc = strlen(fusepath) > 512 - 1 - strlen(myconf.path);
    if (must_malloc) {
        /* Allocate space for full mirrored pathname: base + path + \0. */
        mypath = KFS_MALLOC(strlen(fusepath) + strlen(myconf.path) + 1);
        if (mypath == NULL) {
            kfs_error("malloc(): %s", strerror(errno));
            return -errno;
        }
    }
    strcpy(mypath, myconf.path); /* Careful: buffer overflow prone. */
    strcpy(mypath + strlen(myconf.path), fusepath); /* Re: careful. */
    kfs_debug("mypath = %s", mypath);

    ret = stat(mypath, stbuf);
    if (ret == -1) {
        ret = -errno;
    }

    if (must_malloc) {
        mypath = KFS_FREE(mypath);
    }

    return ret;
}

/**
 * Directories.
 */

static int
kenny_opendir(const char *path, struct fuse_file_info *fi)
{
    struct kenny_fh *fh = NULL;

    fh = KFS_MALLOC(sizeof(*fh));
    if (fh == NULL) {
        kfs_error("malloc(): %s", strerror(errno));
        return -errno;
    }
    fi->fh = fh_kenny2fuse(fh);
    fh->dir = opendir(path);
    if (fh->dir == NULL) {
        kfs_error("opendir(): %s", strerror(errno));
        return -errno;
    }
    return 0;
}

static int
kenny_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
        off_t offset, struct fuse_file_info *fi)
{
    struct stat mystat;
    struct kenny_fh *fh = NULL;
    struct dirent *mydirent = NULL;
    int ret;

    fh = fh_fuse2kenny(fi->fh);
    mydirent = readdir(fh->dir);
    if (mydirent == NULL) {
        kfs_error("readdir(): %s", strerror(errno));
        return -errno;
    }
    ret = fstat(dirfd(fh->dir), &mystat);
    if (ret == -1) {
        kfs_error("fstat(): %s", strerror(errno));
        return -errno;
    }
    ret = filler(buf, mydirent->d_name, &mystat, offset);
    return -ret;
}

static int
kenny_releasedir(const char *path, struct fuse_file_info *fi)
{
    (void) path;

    struct kenny_fh *fh = NULL;
    int ret;

    fh = fh_fuse2kenny(fi->fh);
    ret = closedir(fh->dir);
    if (ret == -1) {
        kfs_error("closedir(): %s", strerror(errno));
        return -errno;
    }
    fh->dir = NULL;
    fh = KFS_FREE(fh);
    fi->fh = 0;

    return 0;
}

static int
kenny_open(const char *path, struct fuse_file_info *fi)
{
    if (strcmp(path, kenny_path) != 0) {
        return -ENOENT;
    }

    if ((fi->flags & 3) != O_RDONLY) {
        return -EACCES;
    }

    return 0;
}

static int
kenny_read(const char *path, char *buf, size_t size, off_t offset,
        struct fuse_file_info *fi)
{
    /* Unused arguments. */
    (void) *fi;

    size_t len = 0;

    if (strcmp(path, kenny_path) != 0) {
        return -ENOENT;
    }

    len = strlen(kenny_str);
    if (offset >= len) {
        return 0;
    }
    memcpy(buf, kenny_str + offset, min(size, len - offset));

    return size;
}

static struct fuse_operations kenny_oper = {
    .getattr = kenny_getattr,
    .opendir = kenny_opendir,
    .readdir = kenny_readdir,
    .releasedir = kenny_releasedir,
    .open = kenny_open,
    .read = kenny_read,
};

/**
 * Process command line arguments.
 */
static int
kenny_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs)
{
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

    kfs_info("Starting KennyFS version %s.", KENNYFS_VERSION);
    memset(&myconf, 0, sizeof(myconf));
    ret = fuse_opt_parse(&args, &myconf, kenny_opts, kenny_opt_proc);
    if (ret == -1 || myconf.path == NULL) {
        return EXIT_FAILURE;
    }
    kenny_str = myconf.path;
    ret = fuse_main(args.argc, args.argv, &kenny_oper, NULL);
    fuse_opt_free_args(&args);
    if (ret != 0) {
        kfs_warning("KennyFS exited with value %d.", ret);
    } else {
        kfs_info("KennyFS exited succesfully.");
    }

    return ret;
}
