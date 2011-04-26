#ifndef KFS_LOGGING_H
#define KFS_LOGGING_H

#include <stdio.h>

#include "kfs.h"

/**
 * Defines logging functions for the KennyFS project. Provides the following
 * levels:
 * - KFS_MINLOG_TRACE
 * - KFS_MINLOG_DEBUG
 * - KFS_MINLOG_INFO
 * - KFS_MINLOG_WARNING
 * - KFS_MINLOG_ERROR
 * - KFS_MINLOG_SILENT
 * Logging functions are available for the first five levels: KFS_DEBUG(),
 * KFS_INFO(), etc.  The KFS_MINLOG_<LEVEL> macro can be used to select which
 * functions are defined. If NDEBUG is defined the default logging level is
 * WARNING, otherwise it is DEBUG.
 */

#ifdef KFS_LOG_ADVANCED
#  error "KFS_LOG_ADVANCED defined before feature test."
#endif
#if (defined(__GNUC__) || \
    (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L))
#  define KFS_LOG_ADVANCED
#endif

#define kfs_do_nothing(...) ((void) (0))

#ifdef KFS_LOG_ADVANCED
#  define KFS_LOGWRAP(lvl, ...) \
      kfs_log(lvl, stderr, __FILE__, __LINE__, __func__, __VA_ARGS__)
#else
#  define KFS_LOGWRAP(lvl, ...) \
      kfs_log(lvl, stderr, __VA_ARGS__)
#endif

/* Default logging capacity depends on NDEBUG macro. */
#if ! (defined(KFS_MINLOG_TRACE) \
    || defined(KFS_MINLOG_DEBUG) \
    || defined(KFS_MINLOG_INFO) \
    || defined(KFS_MINLOG_WARNING) \
    || defined(KFS_MINLOG_ERROR) \
    || defined(KFS_MINLOG_SILENT))
#  ifdef NDEBUG
#    define KFS_MINLOG_WARNING
#  else
#    define KFS_MINLOG_TRACE
#  endif
#endif

#define KFS_DEBUG kfs_do_nothing
#define KFS_INFO kfs_do_nothing
#define KFS_WARNING kfs_do_nothing
#define KFS_ERROR kfs_do_nothing
#define KFS_ENTER() ((void) 0)
#define KFS_RETURN(ret) return ret

#ifndef KFS_MINLOG_SILENT
#  undef KFS_ERROR
#  define KFS_ERROR(...) KFS_LOGWRAP(KFS_LOGLVL_ERROR, __VA_ARGS__)
#  ifndef KFS_MINLOG_ERROR
#    undef KFS_WARNING
#    define KFS_WARNING(...) KFS_LOGWRAP(KFS_LOGLVL_WARNING, __VA_ARGS__)
#    ifndef KFS_MINLOG_WARNING
#      undef KFS_INFO
#      define KFS_INFO(...) KFS_LOGWRAP(KFS_LOGLVL_INFO, __VA_ARGS__)
#      ifndef KFS_MINLOG_INFO
#        undef KFS_DEBUG
#        define KFS_DEBUG(...) KFS_LOGWRAP(KFS_LOGLVL_DEBUG, __VA_ARGS__)
         /* This only makes sense in the presence of __func__ and friends. */
#        if !defined(KFS_MINLOG_DEBUG) && defined(KFS_LOG_ADVANCED)
#          undef KFS_ENTER
#          undef KFS_RETURN
           /*
            * TODO: This could use some nice indenting to represent the call
            * depth.
            */
#          define KFS_ENTER() KFS_LOGWRAP(KFS_LOGLVL_TRACE, "enter")
#          define KFS_RETURN(ret) do { KFS_LOGWRAP(KFS_LOGLVL_TRACE, "return");\
                                       return ret; } while (0)
#        endif
#      endif
#    endif
#  endif
#endif

enum kfs_loglevel {
    KFS_LOGLVL_TRACE,
    KFS_LOGLVL_DEBUG,
    KFS_LOGLVL_INFO,
    KFS_LOGLVL_WARNING,
    KFS_LOGLVL_ERROR,
    KFS_LOGLVL_CRITICAL,
    KFS_LOGLVL_SILENT
};

void kfs_log(enum kfs_loglevel level, FILE *logdest,
#ifdef KFS_LOG_ADVANCED
        const char *file, unsigned int line, const char *func,
#endif
        const char *fmt, ...);

/* Global log level. */
extern enum kfs_loglevel kfs_loglevel;

#endif
