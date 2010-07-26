#ifndef KFS_LOGGING_H
#define KFS_LOGGING_H

#include <assert.h>
#include <stdio.h>

#include "kfs.h"

/**
 * Defines logging functions for the KennyFS project. Provides the following
 * levels:
 * - KFS_LOG_TRACE
 * - KFS_LOG_DEBUG
 * - KFS_LOG_INFO
 * - KFS_LOG_WARNING
 * - KFS_LOG_ERROR
 * - KFS_LOG_SILENT
 * Logging functions are available for the first five levels: KFS_DEBUG(),
 * KFS_INFO(), etc.  The KFS_LOG_<LEVEL> macro can be used to select which
 * functions are defined. If NDEBUG is defined the default logging level is
 * WARNING, otherwise it is DEBUG.
 */

#define KFS_ASSERT assert
/* KFS_NASSERT is executed verbatim in NON-debugging mode. */
#ifdef NDEBUG
#  define KFS_NASSERT(x) x
#else
#  define KFS_NASSERT(X) ((void) 0)
#endif

#define kfs_do_nothing(...) ((void) (0))

#define kfs_log_simple(level, fmt, ...) \
            fprintf(stderr, level ": " fmt "\n", ## __VA_ARGS__)
/* TODO: Check if %ld is a legal printf formatter for struct timeval.tv_usec. */
#define kfs_log_time(level, fmt, ...) do { \
            struct timeval tv; \
            (void) gettimeofday(&tv, NULL); \
            kfs_log_simple("%010jd.%06ld " level, fmt, \
                    (intmax_t) tv.tv_sec, tv.tv_usec, ## __VA_ARGS__); \
            } while (0)
#define kfs_log_full(level, fmt, ...) \
            kfs_log_time("[kfs_" level "] " __FILE__ ":%d %s", fmt, __LINE__, \
                    __func__, ## __VA_ARGS__)
#define kfs_log kfs_log_do_nothing

/* Default logging level depends on NDEBUG macro. */
#if ! (defined(KFS_LOG_TRACE) \
    || defined(KFS_LOG_DEBUG) \
    || defined(KFS_LOG_INFO) \
    || defined(KFS_LOG_WARNING) \
    || defined(KFS_LOG_ERROR) \
    || defined(KFS_LOG_SILENT))
#  ifdef NDEBUG
#    define KFS_LOG_WARNING
#  else
#    define KFS_LOG_DEBUG
#  endif
#endif

#define KFS_DEBUG kfs_do_nothing
#define KFS_INFO kfs_do_nothing
#define KFS_WARNING kfs_do_nothing
#define KFS_ERROR kfs_do_nothing

#ifndef KFS_LOG_SILENT
#  undef kfs_log
#  define kfs_log kfs_log_simple
#  undef KFS_ERROR
#  define KFS_ERROR(...) kfs_log("ERROR", __VA_ARGS__)
#  ifndef KFS_LOG_ERROR
#    undef KFS_WARNING
#    define KFS_WARNING(...) kfs_log("WARNING", __VA_ARGS__)
#    ifndef KFS_LOG_WARNING
#      include <sys/time.h>
#      include <stdint.h>
#      undef kfs_log
#      define kfsLog kfs_log_time
#      undef KFS_INFO
#      define KFS_INFO(...) kfs_log("info", __VA_ARGS__)
#      ifndef KFS_LOG_INFO
#        if (defined(__GNUC__) || \
            (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L))
#          undef kfs_log
#          define kfs_log kfs_log_full
#        endif
#        undef KFS_DEBUG
#        define KFS_DEBUG(...) kfs_log("debug", __VA_ARGS__)
#      endif
#    endif
#  endif
#endif

#endif /* _KFS_LOGGING_H */
