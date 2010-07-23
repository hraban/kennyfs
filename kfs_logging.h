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

#define KFS_DEBUG(...) kfs_log("debug", __VA_ARGS__)
#define KFS_INFO(...) kfs_log("info", __VA_ARGS__)
#define KFS_WARNING(...) kfs_log("WARNING", __VA_ARGS__)
#define KFS_ERROR(...) kfs_log("ERROR", __VA_ARGS__)

#ifndef KFS_LOG_SILENT
#  undef kfs_log
#  if defined(KFS_LOG_ERROR) || defined(KFS_LOG_WARNING)
#    define kfs_log kfs_log_simple
#  else
#    include <sys/time.h>
#    include <stdint.h>
#    if defined(KFS_LOG_INFO)
#      define kfs_log kfs_log_time
#    else
#      if (defined(__GNUC__) || \
          (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L))
#        define kfs_log kfs_log_full
#      else
#        define kfs_log kfs_log_time
#      endif
#    endif
#  endif
#endif

#endif /* _KFS_LOGGING_H */
