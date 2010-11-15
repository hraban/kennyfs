#ifndef KENNYFS_H
#define KENNYFS_H

#include <assert.h>
/* Needed for abort(). */
#include <stdlib.h>

#include "kfs_logging.h"

#define KFS_VERSION "0.0"

/* Do not use the inline keyword outside C99. */
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L
#  define inline
#endif

/* TODO: This could use some nice indenting to represent the call depth. */
#define KFS_ENTER() ((void) (0))
#define KFS_RETURN(ret) return ret
#ifdef KFS_LOG_TRACE
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
/** Prefix to all extended attributes used by kennyfs. */
#define KFS_XATTR_NS "user.com.kennyfs"
/** All permission bits. */
#define PERM7777 (S_IRWXU | S_IRWXG | S_IRWXO | S_ISUID | S_ISGID | S_ISVTX)
#define PERM0600 (S_IRUSR | S_IWUSR)
#define PERM0700 (S_IRUSR | S_IWUSR | S_IXUSR)

#ifndef htonll
#  ifdef _BIG_ENDIAN
#    define htonll(x) (x)
#    define ntohll(x) (x)
#  else
#    define htonll(x) ((((uint64_t)htonl(x)) << 32) + htonl((x) >> 32))
#    define ntohll(x) ((((uint64_t)ntohl(x)) << 32) + ntohl((x) >> 32))
#  endif
#endif
/**
 * Perform file operation, after setting proper state. Example usage:
 *
 * int
 * somefunc(struct kfs_brick subv, kfs_context_t co)
 * {
 *   int ret;
 *
 *   KFS_DO_OPER(ret = , subv, unlink, co, "/foo");
 *   if (ret == 0) {
 *     printf("Success.");
 *   } else {
 *     printf("Failure.");
 *   }
 *
 *   return ret;
 * }
 *
 * The odd notation is a direct result of the constraints of the C syntax and
 * its preprocessor, combined with a "explicit is better than implicit" mantra.
 * At least, somebody reading this will know that something weird is going on
 * with ret.
 */
#define KFS_DO_OPER(ret, brick, op, co, ...) \
    do { void *_KFS_backup = (co)->priv; \
        (co)->priv = (brick)->private_data; \
        ret (brick)->oper->op(co, ## __VA_ARGS__); \
        (co)->priv = _KFS_backup; \
    } while (0)
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

#define KFS_ABORT(x) do {kfs_log("CRITICAL", x); abort(); } while (0)
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
