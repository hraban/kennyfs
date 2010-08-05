/**
 * Library that manages dynamic loading of bricks and reading kfs configuration
 * files.
 */

#define FUSE_USE_VERSION 29

#include "kfs_loadbrick.h"

#include <dlfcn.h>
#include <fuse.h>
#include <string.h>

#include "minini/minini.h"

#include "kfs.h"
#include "kfs_logging.h"
#include "kfs_memory.h"
#include "kfs_misc.h"

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
 * Get the path corresponding to given brick name. On success the brickpath
 * is returned. On error NULL is returned.
 */
static const char *
get_brick_path(const char brickname[])
{
    uint_t i = 0;

    KFS_ENTER();

    KFS_ASSERT(brickname != NULL && brickname[0] != '\0');
    for (i = 0; i < _BRICK_NUM; i++) {
        if (strcmp(brickname, BRICK_NAMES[i]) == 0) {
            KFS_RETURN(BRICK_PATHS[i]);
        }
    }

    KFS_RETURN(NULL);
}

/**
 * Get a pointer to the API for the root brick and initialise all bricks in the
 * chain. A leading ~ in the path is expanded to the environment variable HOME.
 * TODO: Actually only the root is initialised for now, should be fixed.
 */
struct kfs_brick_api *
get_root_brick(const char *conffile)
{
    char brickname[16] = {'\0'};
    kfs_brick_getapi_f brick_getapi_f = NULL;
    const struct kfs_brick_api *orig_api = NULL;
    struct kfs_brick_api *my_api = NULL;
    const char *brickpath = NULL;
    const char *homedir = NULL;
    char *kfsconf = NULL;
    void *dynhandle = NULL;
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(conffile != NULL);
    /* Expand tilde to user's home dir. */
    if (conffile[0] == '~') {
        homedir = getenv("HOME");
        if (homedir == NULL) {
            KFS_ERROR("Configuration file specified with ~ but environment "
                      "variable HOME is not set.");
            KFS_RETURN(NULL);
        }
        kfsconf = kfs_strcat(homedir, conffile + 1);
    } else {
        kfsconf = kfs_strcpy(conffile);
    }
    /* Read the configuration file. */
    ret = ini_gets("brick_root", "type", "", brickname, NUMELEM(brickname),
            kfsconf);
    if (ret == 0) {
        KFS_ERROR("Failed to parse configuration file %s", kfsconf);
        KFS_RETURN(NULL);
    }
    ret = 0;
    brickpath = get_brick_path(brickname);
    if (brickpath == NULL) {
        KFS_ERROR("Brick type not regocnized: '%s'.", brickname);
        KFS_RETURN(NULL);
    }
    /* Load the brick. */
    dynhandle = dlopen(brickpath, RTLD_NOW | RTLD_LOCAL);
    if (dynhandle == NULL) {
        KFS_ERROR("Loading brick failed: %s", dlerror());
        KFS_RETURN(NULL);
    }
    brick_getapi_f = dlsym(dynhandle, "kfs_brick_getapi");
    if (brick_getapi_f == NULL) {
        KFS_ERROR("Loading brick failed: %s", dlerror());
        KFS_RETURN(NULL);
    }
    orig_api = brick_getapi_f();
    my_api = KFS_MALLOC(sizeof(*my_api));
    if (my_api == NULL) {
        KFS_RETURN(NULL);
    }
    ret = orig_api->init(kfsconf, "brick_root");
    if (ret == -1) {
        KFS_ERROR("Preparing brick failed, could not start: code %d.", ret);
        KFS_RETURN(NULL);
    }
    kfsconf = KFS_FREE(kfsconf);
    *my_api = *orig_api;
    my_api->passport.handle = dynhandle;

    KFS_RETURN(my_api);
}

/**
 * Clean up resources opened by get_root_brick().
 */
void
del_root_brick(struct kfs_brick_api *brick_api)
{
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(brick_api != NULL && brick_api->passport.handle != NULL);
    brick_api->halt();
    ret = dlclose(brick_api->passport.handle);
    if (ret != 0) {
        KFS_WARNING("Failed to close dynamically loaded library: %s",
                dlerror());
    }

    KFS_RETURN();
}

