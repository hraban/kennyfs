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
#include "kfs_misc.h"
#include "kfs_backend_posix.h"

#define KENNYFS_OPT(t, p, v) {t, offsetof(struct kenny_conf, p), v}

/**
 * Configuration variables for this frontend.
 */
struct kenny_conf {
    char *path;
    char *backend;
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
    KENNYFS_OPT("backend=%s", backend, 0),
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

/***** Public functions. *****/

int
main(int argc, char *argv[])
{
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    struct kenny_conf conf;
    struct kfs_backend_posix_args posixbackend_args;
    struct kfs_backend_api posixbackend_api;
    int ret = 0;
    struct fuse_operations kenny_oper;
    kfs_backend_getapi_f posixbackend_getapi_f = NULL;
    void *lib_handle = NULL;
    char *errorstr = NULL;

    ret = 0;
    KFS_INFO("Starting KennyFS version %s.", KFS_VERSION);
    /* Parse the command line. */
    memset(&conf, 0, sizeof(conf));
    ret = fuse_opt_parse(&args, &conf, kenny_opts, kenny_opt_proc);
    if (conf.path == NULL || conf.backend == NULL) {
        KFS_ERROR("Error parsing commandline. See %s --help.", argv[0]);
        ret = -1;
    }
    if (ret == 0) {
        /* Load the backend. */
        lib_handle = dlopen(conf.backend, RTLD_NOW | RTLD_LOCAL);
        if (lib_handle != NULL) {
            posixbackend_getapi_f = dlsym(lib_handle, "kenny_getapi");
            if (posixbackend_getapi_f != NULL) {
                posixbackend_api = posixbackend_getapi_f();
            } else {
                ret = -1;
            }
        } else {
            ret = -1;
        }
        errorstr = dlerror();
        if (errorstr != NULL) {
            KFS_ERROR("Loading backend failed: %s", errorstr);
        }
    }
    if (ret == 0) {
        /* Get the KennyFS API getter. */
        if (posixbackend_getapi_f == NULL) {
            errorstr = dlerror();
            if (errorstr != NULL) {
                KFS_ERROR("Loading backend failed: %s", errorstr);
            }
            ret = -1;
        } else {
        }
    }
    if (ret == 0) {
        /* Initialize the backend. */
        posixbackend_args.path = conf.path;
        ret = posixbackend_api.init(posixbackend_args);
        if (ret != 0) {
            KFS_ERROR("Backend failed with code %d.", ret);
        }
    }
    if (ret == 0) {
        /* Run the backend and start FUSE. */
        kenny_oper = posixbackend_api.getfuncs();
        ret = fuse_main(args.argc, args.argv, &kenny_oper, NULL);
        /* Clean everything up. */
        fuse_opt_free_args(&args);
        posixbackend_api.halt();
        dlclose(lib_handle);
    }
    if (ret == 0) {
        KFS_INFO("KennyFS exited succesfully.");
    } else {
        KFS_WARNING("KennyFS exited with value %d.", ret);
    }

    exit(ret);
}
