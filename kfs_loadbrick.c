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

#define MAX_SUBVOLUMES 100U

#ifndef KFS_BRICK_PATH_PREFIX
#  define KFS_BRICK_PATH_PREFIX ""
#endif

/** Private data for resource tracking. */
struct kfs_loadbrick_priv {
    /** For every subvolume: an API struct. */
    struct kfs_brick subvolumes[MAX_SUBVOLUMES];
    /** Array of pointers to subvolumes-nodes. */
    struct kfs_loadbrick_priv *child_nodes[MAX_SUBVOLUMES];
    const struct kfs_brick_api *brick_api;
    void *dload_handle;
    void *private_data;
    size_t num_subvolumes;
};

const char BRICK_PATH_FMT[] = KFS_BRICK_PATH_PREFIX "%s_brick/libkfs_brick_%s.so";

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
 * is returned. On error NULL is returned. The returned string is freshly
 * allocated and must eventually be free'd.
 */
static char *
get_brick_path(const char brickname[])
{
    char *path = NULL;

    KFS_ENTER();

    path = kfs_sprintf(BRICK_PATH_FMT, brickname, brickname);

    KFS_RETURN(path);
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
    void *private_data = NULL;
    uint_t i = 0;

    KFS_ENTER();

    for (i = 0; i < priv->num_subvolumes; i++) {
        priv->subvolumes[i].name = KFS_FREE(priv->subvolumes[i].name);
        private_data = priv->subvolumes[i].private_data;
        priv->child_nodes[i]->brick_api->halt(private_data);
        priv->child_nodes[i] = del_any_brick(priv->child_nodes[i]);
    }
    priv = KFS_FREE(priv);

    KFS_RETURN(priv);
}

static struct kfs_loadbrick_priv *
get_any_brick(const char *conffile, const char *section)
{
    kfs_brick_getapi_f brick_getapi_f = NULL;
    const struct kfs_brick_api *brick_api = NULL;
    char *brickpath = NULL;
    struct kfs_loadbrick_priv *priv = NULL;
    struct kfs_brick *subvolume = NULL;
    char *subvolume_name = NULL;
    uint_t i = 0;
    void *dynhandle = NULL;
    char *buf = NULL;
    char *strtokcontext = NULL;

    KFS_ENTER();

    /* Read the configuration file. */
    buf = kfs_ini_gets(conffile, section, "type");
    if (buf == NULL) {
        KFS_ERROR("Failed to parse configuration file %s", conffile);
        KFS_RETURN(NULL);
    }
    brickpath = get_brick_path(buf);
    if (brickpath == NULL) {
        KFS_ERROR("Failed to load brick of type: '%s'.", buf);
        buf = KFS_FREE(buf);
        KFS_RETURN(NULL);
    }
    buf = KFS_FREE(buf);
    /* Dynamically load the brick. */
    dynhandle = dlopen(brickpath, RTLD_NOW | RTLD_LOCAL);
    brickpath = KFS_FREE(brickpath);
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
        subvolume_name = strtok_r(buf, ",", &strtokcontext);
        if (subvolume_name == NULL) {
            KFS_ERROR("Invalid `subvolumes' value for brick %s in file %s.",
                    section, conffile);
            priv = del_any_brick(priv);
            buf = KFS_FREE(buf);
            KFS_RETURN(NULL);
        }
        for (i = 0; i < MAX_SUBVOLUMES; i++) {
            subvolume_name = kfs_stripspaces(subvolume_name,
                                                    strlen(subvolume_name));
            KFS_DEBUG("Create subvolume nr %u for brick %s: `%s'.", i + 1,
                    section, subvolume_name);
            priv->child_nodes[i] = get_any_brick(conffile, subvolume_name);
            if (priv->child_nodes[i] == NULL) {
                priv = del_any_brick(priv);
                buf = KFS_FREE(buf);
                KFS_RETURN(NULL);
            }
            subvolume = &(priv->subvolumes[i]);
            subvolume->oper = priv->child_nodes[i]->brick_api->getfuncs();
            subvolume->private_data = priv->child_nodes[i]->private_data;
            subvolume->name = kfs_strcpy(subvolume_name);
            priv->num_subvolumes += 1;
            /* Name of the next subvolume. */
            subvolume_name = strtok_r(NULL, ",", &strtokcontext);
            if (subvolume_name == NULL) {
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
    priv->private_data = brick_api->init(conffile, section,
            priv->num_subvolumes, priv->subvolumes);
    if (priv->private_data == NULL) {
        KFS_ERROR("Preparing brick `%s' failed.", section);
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
    /** The operation handlers as per the KennyFS API. */
    const struct kfs_operations *oper = NULL;
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
    oper = priv->brick_api->getfuncs();
    brick->oper = oper;
    brick->private_data = priv->private_data;
    brick->_lbpriv = priv;

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
    priv = brick->_lbpriv;
    dynhandle = priv->dload_handle;
    priv->brick_api->halt(priv->private_data);
    priv = del_any_brick(priv);
    brick->_lbpriv = priv;
    ret = dlclose(dynhandle);
    if (ret != 0) {
        KFS_WARNING("Failed to close dynamically loaded library: %s",
                dlerror());
    }

    KFS_RETURN(brick);
}
