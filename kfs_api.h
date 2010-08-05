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
};

/**
 * Function that gets API (above three) of a brick. Every KennyFS brick should
 * provide a public function of this type with the name "kfs_brick_getapi".
 */
typedef const struct kfs_brick_api * (* kfs_brick_getapi_f)(void);

#endif
