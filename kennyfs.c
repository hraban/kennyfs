#define KENNYFS_VERSION "0.0"
#define FUSE_USE_VERSION 26

#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <fuse_opt.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kfs_logging.h"

#define KENNYFS_OPT(t, p, v) {t, offsetof(struct kenny_conf, p), v}

struct kenny_conf {
    char *path;
};

enum {
    KEY_HELP,
    KEY_VERSION,
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
 * Mirrors all calls to the source dir.
 */
static int
kenny_getattr(const char *fusepath, struct stat *stbuf)
{
    int ret = 0;
    /* On-stack buffer for paths of limited length. Otherwise: malloc(). */
    char pathbuf[512];
    char *mypath = pathbuf;

    memset(stbuf, 0, sizeof(*stbuf));
    if (strlen(fusepath) > 512 - 2 - strlen(myconf.path)) {
        /* Allocate space for full mirrored pathname: base + / + path + \0. */
        mypath = malloc(strlen(fusepath) + strlen(myconf.path) + 2);
        if (mypath == NULL || 1) {
            kfs_error("malloc(): %s", strerror(errno));
            return -ENOMEM;
        }
    }
    strcpy(mypath, myconf.path); /* Careful: buffer overflow prone. */
    mypath[strlen(mypath)] = '/';
    strcpy(mypath + strlen(fusepath) + 1, fusepath); /* Re: careful. */

    ret = stat(mypath, stbuf);
    if (ret == -1) {
        ret = -errno;
    }

    return ret;
}

static int
kenny_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
        off_t offset, struct fuse_file_info *fi)
{
    /* Unused arguments. */
    (void) offset;
    (void) fi;

    if (strcmp(path, "/") != 0) {
        return -ENOENT;
    }

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    filler(buf, kenny_path + 1, NULL, 0);
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
    .readdir = kenny_readdir,
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

    memset(&myconf, 0, sizeof(myconf));
    ret = fuse_opt_parse(&args, &myconf, kenny_opts, kenny_opt_proc);
    if (ret == -1 || myconf.path == NULL) {
        return EXIT_FAILURE;
    }
    kenny_str = myconf.path;
    ret = fuse_main(args.argc, args.argv, &kenny_oper, NULL);
    fuse_opt_free_args(&args);

    return ret;
}
