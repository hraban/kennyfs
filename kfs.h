#ifndef KENNYFS_H
#define KENNYFS_H

#include <assert.h>

#define KFS_VERSION "0.0"

/* Do not use the inline keyword outside C99. */
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L
#  define inline
#endif

/* TODO: This could use some nice indenting to represent the call depth. */
#define KFS_ENTER() ((void) (0))
#define KFS_RETURN(ret) return ret
#ifdef KFS_LOG_TRACE
#  include "kfs_logging.h"
#  if defined(__GNUC__) || \
      defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#    undef KFS_ENTER
#    undef KFS_RETURN
     /* Function name is already printed by kfs_log in trace mode. */
#    define KFS_ENTER() kfs_log("trace", "enter")
#    define KFS_RETURN(ret) do { kfs_log("trace", "return"); return ret; \
                               } while (0)
#  endif
#endif

/**
 * Many functions use this macro in allocating a buffer on the stack to build a
 * full pathname. When it is exceeded more memory is automatically allocated on
 * the heap. Happens with kfs_bufstrcat().
 */
#define PATHBUF_SIZE 256

#ifndef htonll
#  ifdef _BIG_ENDIAN
#    define htonll(x) (x)
#    define ntohll(x) (x)
#  else
#    define htonll(x) ((((uint64_t)htonl(x)) << 32) + htonl((x) >> 32))
#    define ntohll(x) ((((uint64_t)ntohl(x)) << 32) + ntohl((x) >> 32))
#  endif
#endif

#define KFS_ASSERT assert
/* KFS_NASSERT is executed verbatim in NON-debugging mode. */
#ifdef NDEBUG
#  define KFS_NASSERT(x) x
#else
#  define KFS_NASSERT(X) ((void) 0)
#endif

#ifdef HAVE_XATTR
#  define KFS_USE_XATTR
#endif

typedef unsigned int uint_t;

#endif
