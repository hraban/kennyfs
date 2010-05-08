#ifndef KENNYFS_H
#define KENNYFS_H

#define KFS_VERSION "0.0"

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
  /* "inline" is a keyword */
#else
#  define inline /* nothing */
#endif

/* TODO: This could use some nice indenting to represent the call depth. */
#define KFS_ENTER() ((void) (0))
#define KFS_RETURN(ret) return(ret)
#ifdef KFS_LOG_TRACE
#  include "kfs_logging.h"
#  if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#    undef KFS_ENTER
#    undef KFS_RETURN
#    define KFS_ENTER() kfs_log("trace", ">> %s()", __func__)
#    define KFS_RETURN(ret) do { \
                                kfs_log("trace", "<< %s(): %d", __func__, ret);\
                                return(ret); \
                            } while (0)
#  endif
#endif

#endif
