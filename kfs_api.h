#ifndef KFS_API_H
#define KFS_API_H

/* Typedef size_t is needed. */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
/* In C99 stddef.h is a legal (lighter) substitute to get size_t. */
#  include "stddef.h"
#else
#  include "stdlib.h"
#endif

#include "kfs.h"

/**
 * Platform dependant record that contains all necessary information to identify
 * a dynamically loaded resource. The frontend calls an init-like library
 * function with just a const char * (the name of the desired brick), that init
 * function returns a pointer to a struct kfs_brick_api (see below). Once done
 * with the brick, the frontend calls a cleanup-like function in that same
 * library with as argument that pointer from init(), which then releases all
 * resources. This struct, as part of struct kfs_brick_api, is evidently also
 * passed and can be used to keep track of what resources need to be freed.
 *
 * Since this software is currently only tested on linux, only the dlopen()
 * interface is used (void *).
 */
struct kfs_brick_passport {
    void *handle;
};

/**
 * Function that prepares the brick for operation. The first argument is the
 * path to the configuration file, the second is the section from which this
 * brick is being loaded. They are intended for use with the minini library
 * (linked).
 */
typedef int (* kfs_brick_init_f)(const char *conffile, const char *section);
/** Function to obtain pointer to FUSE callback handlers for this brick. */
typedef const struct fuse_operations * (* kfs_brick_getfuncs_f)(void);
/** Function that shuts down the brick (at least until the next init()). */
typedef void (* kfs_brick_halt_f)(void);

struct kfs_brick_api {
    kfs_brick_init_f init;
    kfs_brick_getfuncs_f getfuncs;
    kfs_brick_halt_f halt;
    /** This element is to be set by the user of the dynamic library. */
    struct kfs_brick_passport passport;
};

/**
 * Function that gets API (above three) of a brick. Every KennyFS brick should
 * provide a public function of this type with the name "kfs_brick_getapi".
 */
typedef const struct kfs_brick_api * (* kfs_brick_getapi_f)(void);

#endif
