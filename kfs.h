#ifndef KENNYFS_H
#define KENNYFS_H

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

typedef unsigned int uint_t;

#endif
