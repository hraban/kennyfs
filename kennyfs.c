/**
 * KennyFS command-line frontend. Lots of values are hard-coded (for now).
 */

#define FUSE_USE_VERSION 29

#include <dlfcn.h>
#include <errno.h>
#include <fuse.h>
#include <fuse_opt.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "minini/minini.h"

#include "kfs.h"
#include "kfs_api.h"
#include "kfs_misc.h"

#define KENNYFS_OPT(t, p, v) {t, offsetof(struct kenny_conf, p), v}

/**
 * Configuration variables.
 */
struct kenny_conf {
    char *kfsconf;
};

enum {
    KEY_HELP,
    KEY_VERSION,
};

enum {
    BRICK_PASS,
    BRICK_POSIX,
    BRICK_TCP,
    _BRICK_NUM,
};

const char * const BRICK_NAMES[] = {
    [BRICK_PASS] = "pass",
    [BRICK_POSIX] = "posix",
    [BRICK_TCP] = "tcp",
};

const char * const BRICK_PATHS[] = {
    [BRICK_PASS] = "pass_brick/libkfs_brick_pass.so",
    [BRICK_POSIX] = "posix_brick/libkfs_brick_posix.so",
    [BRICK_TCP] = "tcp_brick/libkfs_brick_tcp.so",
};

/**
 * Options accepted by this frontend's command-line parser.
 */
static struct fuse_opt kenny_opts[] = {
    KENNYFS_OPT("kfsconf=%s", kfsconf, 0),
    FUSE_OPT_KEY("-h", KEY_HELP),
    FUSE_OPT_KEY("--help", KEY_HELP),
    FUSE_OPT_KEY("-v", KEY_VERSION),
    FUSE_OPT_KEY("--version", KEY_VERSION),
    FUSE_OPT_END
};

/** Default location of the kennyfs configuration file. */
const char KFSCONF_DEFAULT_PATH[] = "~/.kennyfs.ini";
/** Global reference to dynamically loaded library. TODO: Unnecessary global. */
void *lib_handle = NULL;

/**
 * Get the path corresponding to given brick name. On success the brickpath
 * argument is set to point to the path and a unique ID for this brick is
 * returned. On error the pointer will point to NULL and the return value is
 * unspecified.
 */
static uint_t
get_brick_path(const char brickname[], const char **brickpath)
{
    uint_t i = 0;

    KFS_ENTER();

    KFS_ASSERT(brickpath != NULL && brickname != NULL && brickname[0] != '\0');
    *brickpath = NULL;
    for (i = 0; i < _BRICK_NUM; i++) {
        if (strcmp(brickname, BRICK_NAMES[i]) == 0) {
            *brickpath = BRICK_PATHS[i];
            break;
        }
    }

    KFS_RETURN(i);
}

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
                "    -o kfsconf=PATH  configuration file\n"
                "\n",
                outargs->argv[0]);
        fuse_opt_add_arg(outargs, "-ho");
        fuse_main(outargs->argc, outargs->argv, NULL, NULL);
        exit(EXIT_SUCCESS);
    }

    return 1;
}

/**
 * Get a pointer to the API for the root brick and initialise all bricks in the
 * chain. TODO: Actually only the root is initialised for now, should be fixed.
 */
static const struct kfs_brick_api *
get_root_brick(const char *conffile)
{
    char brickname[16] = {'\0'};
    kfs_brick_getapi_f brick_getapi_f = NULL;
    const struct kfs_brick_api *brick_api = NULL;
    const char *brickpath = NULL;
    uint_t brick = 0;
    int ret = 0;

    KFS_ENTER();

    /* Read the configuration file. */
    ret = ini_gets("brick_root", "type", "", brickname, NUMELEM(brickname),
            conffile);
    if (ret == 0) {
        KFS_ERROR("Failed to parse configuration file %s", conffile);
        KFS_RETURN(NULL);
    }
    ret = 0;
    brick = get_brick_path(brickname, &brickpath);
    if (brickpath == NULL) {
        KFS_ERROR("Brick type not regocnized: '%s'.", brickname);
        KFS_RETURN(NULL);
    }
    /* Load the brick. */
    lib_handle = dlopen(brickpath, RTLD_NOW | RTLD_LOCAL);
    if (lib_handle == NULL) {
        KFS_ERROR("Loading brick failed: %s", dlerror());
        KFS_RETURN(NULL);
    }
    brick_getapi_f = dlsym(lib_handle, "kfs_brick_getapi");
    if (brick_getapi_f == NULL) {
        KFS_ERROR("Loading brick failed: %s", dlerror());
        KFS_RETURN(NULL);
    }
    brick_api = brick_getapi_f();
    ret = brick_api->init(conffile, "brick_root");
    if (ret == -1) {
        KFS_ERROR("Preparing brick failed, could not start: code %d.", ret);
        KFS_RETURN(NULL);
    }

    KFS_RETURN(brick_api);
}

/**
 * Clean up resources opened by get_root_brick().
 */
void
del_root_brick(const struct kfs_brick_api *brick_api)
{
    int ret = 0;

    KFS_ENTER();
    brick_api->halt();
    KFS_ASSERT(lib_handle != NULL);

    ret = dlclose(lib_handle);
    if (ret != 0) {
        KFS_WARNING("Failed to close dynamically loaded library: %s",
                dlerror());
    }

    KFS_RETURN();
}

/**
 * "Real" main() function, actual main() is just a wrapper for debugging the
 * return value.
 */
static int
main_(int argc, char *argv[])
{
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    struct kenny_conf conf;
    int ret = 0;
    const struct fuse_operations *kenny_oper = NULL;
    const struct kfs_brick_api *brick_api = NULL;
    const char *homedir = NULL;
    char *kfsconf = NULL;
    void *p = NULL;

    KFS_ENTER();

    KFS_INFO("Starting KennyFS version %s.", KFS_VERSION);
    /* Parse the command line. */
    memset(&conf, 0, sizeof(conf));
    ret = fuse_opt_parse(&args, &conf, kenny_opts, kenny_opt_proc);
    if (ret == -1) {
        KFS_ERROR("Parsing options failed.");
        KFS_RETURN(-1);
    }
    if (conf.kfsconf != NULL) {
        kfsconf = kfs_strcpy(conf.kfsconf);
    } else {
        kfsconf = kfs_strcpy(KFSCONF_DEFAULT_PATH);
    }
    /* Expand tilde to user's home dir. */
    if (kfsconf[0] == '~') {
        homedir = getenv("HOME");
        if (homedir == NULL) {
            KFS_ERROR("Configuration file specified with ~ but environment "
                      "variable HOME is not set.");
            fuse_opt_free_args(&args);
            KFS_RETURN(-1);
        }
        p = kfsconf;
        kfsconf = kfs_strcat(homedir, kfsconf + 1);
        p = KFS_FREE(p);
    }
    brick_api = get_root_brick(kfsconf);
    if (brick_api == NULL) {
        fuse_opt_free_args(&args);
        kfsconf = KFS_FREE(kfsconf);
        KFS_RETURN(-1);
    }
    /* Run the brick and start FUSE. */
    kenny_oper = brick_api->getfuncs();
    ret = fuse_main(args.argc, args.argv, kenny_oper, NULL);
    /* Clean everything up. */
    del_root_brick(brick_api);
    fuse_opt_free_args(&args);
    kfsconf = KFS_FREE(kfsconf);

    KFS_RETURN(ret);
}

int
main(int argc, char *argv[])
{
    int ret = 0;

    ret = main_(argc, argv);
    if (ret == 0) {
        KFS_INFO("KennyFS exited succesfully.");
        exit(EXIT_SUCCESS);
    } else {
        KFS_WARNING("KennyFS exited with value %d.", ret);
        exit(EXIT_FAILURE);
    }

    return -1;
}
