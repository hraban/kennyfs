#ifndef _KFS_LOGGING_H
#define _KFS_LOGGING_H

#include <assert.h>
#include <stdio.h>

#include "kfs.h"

/**
 * Defines logging functions for the KennyFS project. Provides the following
 * levels:
 * - KFS_LOGGING_TRACE
 * - KFS_LOGGING_DEBUG
 * - KFS_LOGGING_INFO
 * - KFS_LOGGING_WARNING
 * - KFS_LOGGING_ERROR
 * - KFS_LOGGING_SILENT
 * Logging functions are available for the first five levels: KFS_DEBUG(),
 * KFS_INFO(), etc.  The KFS_LOGGING_LEVEL macro can be used to select which
 * functions are defined. If NDEBUG is defined the default logging level is
 * WARNING, otherwise it is DEBUG.
 */

#define KFS_ASSERT assert

#define kfs_do_nothing(format, ...) ((void *) (0))
#define kfs_log(level, fmt, ...) fprintf(stderr, level ": " fmt "\n", ## \
        __VA_ARGS__)

#if ! (defined(KFS_LOG_TRACE) \
    || defined(KFS_LOG_DEBUG) \
    || defined(KFS_LOG_INFO) \
    || defined(KFS_LOG_WARNING) \
    || defined(KFS_LOG_ERROR) \
    || defined(KFS_LOG_SILENT))
#ifdef NDEBUG
#  define KFS_LOG_WARNING
#else
#  define KFS_LOG_DEBUG
#endif
#endif


/* Start by defining all debug macros as being in use. */
#define kfs_trace kfs_do_nothing
#define KFS_DEBUG kfs_do_nothing
#define KFS_INFO kfs_do_nothing
#define KFS_WARNING kfs_do_nothing
#define KFS_ERROR kfs_do_nothing

/* Now progressively strip down all unused macros. */
#ifndef KFS_LOG_SILENT
#  undef KFS_ERROR
#  define KFS_ERROR(...) kfs_log("ERROR", __VA_ARGS__)
#  ifndef KFS_LOG_ERROR
#    undef KFS_WARNING
#    define KFS_WARNING(...) kfs_log("WARNING", __VA_ARGS__)
#    ifndef KFS_LOG_WARNING
#      undef KFS_INFO
#      define KFS_INFO(...) kfs_log("info", __VA_ARGS__)
#      ifndef KFS_LOG_INFO
#        undef KFS_DEBUG
#        define KFS_DEBUG(...) kfs_log("debug", __VA_ARGS__)
         /* More explicit logging (for all levels). */
#        undef kfs_log
#        define kfs_log(level, fmt, ...) fprintf(stderr, "[kfs_" level "] " \
                 __FILE__ ":%d %s: " fmt "\n", __LINE__, __func__, ## \
                __VA_ARGS__)
#      endif
#    endif
#  endif
#endif

#endif /* _KFS_LOGGING_H */
