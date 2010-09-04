/**
 * KennyFS command-line frontend.
 */

#define FUSE_USE_VERSION 29

#include <fuse.h>
#include <fuse_opt.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "minini/minini.h"

#include "kfs.h"
#include "kfs_api.h"
#include "kfs_fuseoperglue.h"
#include "kfs_loadbrick.h"
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
 * "Real" main() function, actual main() is just a wrapper for debugging the
 * return value.
 */
static int
main_(int argc, char *argv[])
{
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    const struct fuse_operations *fuse_oper = NULL;
    struct kenny_conf conf;
    struct kfs_loadbrick brick;
    int ret = 0;
    const char *kfsconf = NULL;

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
        kfsconf = conf.kfsconf;
    } else {
        kfsconf = KFSCONF_DEFAULT_PATH;
    }
    ret = get_root_brick(kfsconf, &brick);
    if (ret == -1) {
        fuse_opt_free_args(&args);
        KFS_RETURN(-1);
    }
    fuse_oper = kfs2fuse_operations(brick.oper, brick.private_data);
    /* Run the brick and start FUSE. */
    ret = fuse_main(args.argc, args.argv, fuse_oper, NULL);
    /* Clean everything up. */
    fuse_oper = kfs2fuse_clean(fuse_oper);
    del_root_brick(&brick);
    fuse_opt_free_args(&args);

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
