/**
 * Library that manages dynamic loading of bricks and reading kfs configuration
 * files.
 */

#define FUSE_USE_VERSION 29

/** Required for strtok_r(). */
#define _XOPEN_SOURCE 500

#include "kfs_loadbrick.h"

#include <fuse.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

#include "minini/minini.h"

#include "kfs.h"
#include "kfs_api.h"
#include "kfs_logging.h"
#include "kfs_memory.h"
#include "kfs_misc.h"

enum {
    BRICK_PASS,
    BRICK_POSIX,
    BRICK_TCP,
    _BRICK_NUM,
};

#define MAX_SUBVOLUMES 100

/** Private data for resource tracking. */
struct kfs_loadbrick_priv {
    void *dload_handle;
    const struct kfs_brick_api *brick_api;
    /**
     * Array of pointers to subvolumes-nodes. Must be NULL-terminated, hence
     * the +1.
     */
    struct kfs_loadbrick_priv *subvolumes[MAX_SUBVOLUMES + 1];
    /** For every subvolume: a pointer to its FUSE handlers. */
    const struct fuse_operations *subvolumes_funcs[MAX_SUBVOLUMES + 1];
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
 * Allocate a new, zero'ed private brick.
 */
static struct kfs_loadbrick_priv *
new_kfs_loadbrick_priv(void)
{
    struct kfs_loadbrick_priv *p = NULL;

    KFS_ENTER();

    p = KFS_CALLOC(1, sizeof(*p));

    KFS_RETURN(p);
}

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
 * Close a brick's subvolumes and free all the associated resources.  Note: this
 * closes all subvolumes but actually NOT the brick itself: that should be
 * halted before calling this. A bit cumbersome, but it allows a non-initialised
 * brick to be deleted. It /does/ free the brick's memory, though, and returns
 * a pointer to an unusable memory location.
 */
static struct kfs_loadbrick_priv *
del_any_brick(struct kfs_loadbrick_priv *priv)
{
    uint_t i = 0;

    KFS_ENTER();

    for (i = 0; priv->subvolumes[i] != NULL; i++) {
        priv->subvolumes[i]->brick_api->halt();
        priv->subvolumes[i] = del_any_brick(priv->subvolumes[i]);
    }
    priv = KFS_FREE(priv);

    KFS_RETURN(priv);
}

static struct kfs_loadbrick_priv *
get_any_brick(const char *conffile, const char *section)
{
    kfs_brick_getapi_f brick_getapi_f = NULL;
    const struct kfs_brick_api *brick_api = NULL;
    const char *brickpath = NULL;
    struct kfs_loadbrick_priv *priv = NULL;
    uint_t i = 0;
    void *dynhandle = NULL;
    char *buf = NULL;
    char *strtokcontext = NULL;
    char *subvolume = NULL;
    int ret = 0;

    KFS_ENTER();

    /* Read the configuration file. */
    buf = kfs_ini_gets(conffile, section, "type");
    if (buf == NULL) {
        KFS_ERROR("Failed to parse configuration file %s", conffile);
        KFS_RETURN(NULL);
    }
    brickpath = get_brick_path(buf);
    if (brickpath == NULL) {
        KFS_ERROR("Root brick type not recognized: '%s'.", buf);
        buf = KFS_FREE(buf);
        KFS_RETURN(NULL);
    }
    buf = KFS_FREE(buf);
    /* Dynamically load the brick. */
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
    /* Allocate space for the bookkeeping struct. */
    priv = new_kfs_loadbrick_priv();
    if (priv == NULL) {
        KFS_RETURN(NULL);
    }
    brick_api = brick_getapi_f();
    priv->dload_handle = dynhandle;
    priv->brick_api = brick_api;
    /* Load the subvolumes, if any. */
    buf = kfs_ini_gets(conffile, section, "subvolumes");
    if (buf != NULL) {
        /* Initialize all subvolumes. */
        subvolume = strtok_r(buf, ",", &strtokcontext);
        if (subvolume == NULL) {
            KFS_ERROR("Invalid `subvolumes' value for brick %s in file %s.",
                    section, conffile);
            priv = del_any_brick(priv);
            buf = KFS_FREE(buf);
            KFS_RETURN(NULL);
        }
        for (i = 0; i < MAX_SUBVOLUMES; i++) {
            KFS_DEBUG("Create subvolume nr %u for brick %s: `%s'.", i + 1,
                    section, subvolume);
            priv->subvolumes[i] = get_any_brick(conffile, subvolume);
            if (priv->subvolumes[i] == NULL) {
                priv = del_any_brick(priv);
                buf = KFS_FREE(buf);
                KFS_RETURN(NULL);
            }
            priv->subvolumes_funcs[i] =
                priv->subvolumes[i]->brick_api->getfuncs();
            subvolume = strtok_r(NULL, ",", &strtokcontext);
            if (subvolume == NULL) {
                break;
            }
        }
        buf = KFS_FREE(buf);
        if (i == MAX_SUBVOLUMES) {
            KFS_ERROR("Too many subvolumes for %s (%u max).", section,
                    MAX_SUBVOLUMES);
            priv = del_any_brick(priv);
            KFS_RETURN(NULL);
        }
    }
    /* Initialise the brick. */
    ret = brick_api->init(conffile, section, priv->subvolumes_funcs);
    if (ret == -1) {
        KFS_ERROR("Preparing brick `%s' failed (code %d).", section, ret);
        priv = del_any_brick(priv);
        KFS_RETURN(NULL);
    }

    KFS_RETURN(priv);
}

/**
 * Get a pointer to the API for the root brick and initialise all bricks in the
 * chain. A leading ~ in the path is expanded to the environment variable HOME.
 * TODO: Actually only the root is initialised for now, should be fixed.
 */
int
get_root_brick(const char *conffile, struct kfs_loadbrick *brick)
{
    struct kfs_loadbrick_priv *priv = NULL;
    const char *homedir = NULL;
    char *kfsconf = NULL;

    KFS_ENTER();

    KFS_ASSERT(conffile != NULL);
    /* Expand tilde to user's home dir. */
    if (conffile[0] == '~') {
        homedir = getenv("HOME");
        if (homedir == NULL) {
            KFS_ERROR("Configuration file specified with ~ but environment "
                      "variable HOME is not set.");
            KFS_RETURN(-1);
        }
        kfsconf = kfs_strcat(homedir, conffile + 1);
    } else {
        kfsconf = kfs_strcpy(conffile);
    }
    if (kfsconf == NULL) {
        KFS_RETURN(-1);
    }
    priv = get_any_brick(kfsconf, "brick_root");
    kfsconf = KFS_FREE(kfsconf);
    if (priv == NULL) {
        KFS_RETURN(-1);
    }
    brick->priv = priv;
    brick->oper = priv->brick_api->getfuncs();

    KFS_RETURN(0);
} 

/**
 * Clean up resources opened by get_root_brick().
 */
struct kfs_loadbrick *
del_root_brick(struct kfs_loadbrick *brick)
{
    void *dynhandle = NULL;
    struct kfs_loadbrick_priv *priv = NULL;
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(brick != NULL);
    priv = brick->priv;
    dynhandle = priv->dload_handle;
    priv->brick_api->halt();
    priv = del_any_brick(priv);
    brick->priv = priv;
    ret = dlclose(dynhandle);
    if (ret != 0) {
        KFS_WARNING("Failed to close dynamically loaded library: %s",
                dlerror());
    }

    KFS_RETURN(brick);
}
