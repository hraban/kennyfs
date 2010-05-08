/**
 * KennyFS command-line frontend. Lots of values are hard-coded (for now).
 */

#define FUSE_USE_VERSION 26

#include <dlfcn.h>
#include <errno.h>
#include <fuse.h>
#include <fuse_opt.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "kfs.h"
#include "kfs_api.h"
#include "kfs_misc.h"

#define KENNYFS_OPT(t, p, v) {t, offsetof(struct kenny_conf, p), v}

/**
 * Configuration variables.
 */
struct kenny_conf {
    char *path;
    char *brick;
};

enum {
    KEY_HELP,
    KEY_VERSION,
};

/**
 * Options accepted by this frontend's command-line parser.
 */
static struct fuse_opt kenny_opts[] = {
    KENNYFS_OPT("path=%s", path, 0),
    KENNYFS_OPT("brick=%s", brick, 0),
    FUSE_OPT_KEY("-h", KEY_HELP),
    FUSE_OPT_KEY("--help", KEY_HELP),
    FUSE_OPT_KEY("-v", KEY_VERSION),
    FUSE_OPT_KEY("--version", KEY_VERSION),
    FUSE_OPT_END
};

/*
 * ***** Private functions. *****
 */


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
        fprintf(stderr, "KennyFS version %s\n", KFS_VERSION);
        fuse_opt_add_arg(outargs, "--version");
        fuse_main(outargs->argc, outargs->argv, NULL, NULL);
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
        fuse_main(outargs->argc, outargs->argv, NULL, NULL);
        exit(EXIT_SUCCESS);
    }

    return 1;
}

/**
 * Start up the POSIX backend. Returns 0 on success, -1 on error.
 */
static int
prepare_posix_backend(struct kfs_brick_api *api, char *path)
{
    struct kfs_brick_arg *arg = NULL;
    int ret = 0;

    KFS_ASSERT(api != NULL && path != NULL);
    /* Prepare for initialization of the brick: construct its argument. */
    arg = api->makearg(path);
    if (arg == NULL) {
        KFS_ERROR("Preparing brick failed.");
        ret = -1;
    } else {
        /* Arguments ready: now initialize the brick. */
        ret = api->init(arg);
        if (ret != 0) {
            KFS_ERROR("Brick could not start: code %d.", ret);
            ret = -1;
        }
        /* Initialization is over, free the argument. */
        arg = kfs_brick_delarg(arg);
    }

    return ret;
}

int
main(int argc, char *argv[])
{
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    struct kenny_conf conf;
    struct kfs_brick_api posixbrick_api;
    int ret = 0;
    const struct fuse_operations *kenny_oper = NULL;
    kfs_brick_getapi_f posixbrick_getapi_f = NULL;
    void *lib_handle = NULL;
    char *errorstr = NULL;

    ret = 0;
    KFS_INFO("Starting KennyFS version %s.", KFS_VERSION);
    /* Parse the command line. */
    memset(&conf, 0, sizeof(conf));
    ret = fuse_opt_parse(&args, &conf, kenny_opts, kenny_opt_proc);
    if (conf.path == NULL || conf.brick == NULL) {
        KFS_ERROR("Error parsing commandline. See %s --help.", argv[0]);
        ret = -1;
    }
    if (ret == 0) {
        /* Load the brick. */
        lib_handle = dlopen(conf.brick, RTLD_NOW | RTLD_LOCAL);
        if (lib_handle != NULL) {
            posixbrick_getapi_f = dlsym(lib_handle, "kfs_brick_getapi");
            if (posixbrick_getapi_f != NULL) {
                posixbrick_api = posixbrick_getapi_f();
            } else {
                ret = -1;
            }
        } else {
            ret = -1;
        }
        errorstr = dlerror();
        if (errorstr != NULL) {
            KFS_ERROR("Loading brick failed: %s", errorstr);
        }
    }
    if (ret == 0) {
        ret = prepare_posix_backend(&posixbrick_api, conf.path);
    }
    if (ret == 0) {
        /* Run the brick and start FUSE. */
        kenny_oper = posixbrick_api.getfuncs();
        ret = fuse_main(args.argc, args.argv, kenny_oper, NULL);
        /* Clean everything up. */
        fuse_opt_free_args(&args);
        posixbrick_api.halt();
        dlclose(lib_handle);
    }
    if (ret == 0) {
        KFS_INFO("KennyFS exited succesfully.");
        exit(EXIT_SUCCESS);
    } else {
        KFS_WARNING("KennyFS exited with value %d.", ret);
        exit(EXIT_FAILURE);
    }
}
