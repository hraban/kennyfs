#ifndef _KFS_LOGGING_H
#define _KFS_LOGGING_H

/**
 * Defines logging functions for the KennyFS project. Provides the following
 * levels:
 * - KFS_LOGGING_TRACE
 * - KFS_LOGGING_DEBUG
 * - KFS_LOGGING_INFO
 * - KFS_LOGGING_WARNING
 * - KFS_LOGGING_ERROR
 * - KFS_LOGGING_SILENT
 * Logging functions are available for the first five levels: kfs_debug(),
 * kfs_info(), etc.  The KFS_LOGGING_LEVEL macro can be used to select which
 * functions are defined. If NDEBUG is defined the default logging level is
 * WARNING, otherwise it is DEBUG.
 */

#define kfs_do_nothing(format, ...) ((void *) (0))
#define kfs_log(level, fmt, ...) fprintf(stderr, "%s: " fmt "\n", \
        level, ## __VA_ARGS__)

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
#define kfs_debug kfs_do_nothing
#define kfs_info kfs_do_nothing
#define kfs_warning kfs_do_nothing
#define kfs_error kfs_do_nothing

/* Now progressively strip down all unused macros. */
#ifndef KFS_LOG_SILENT
#  undef kfs_error
#  define kfs_error(...) kfs_log("ERROR", __VA_ARGS__)
#  ifndef KFS_LOG_ERROR
#    undef kfs_warning
#    define kfs_warning(...) kfs_log("WARNING", __VA_ARGS__)
#    ifndef KFS_LOG_WARNING
#      undef kfs_info
#      define kfs_info(...) kfs_log("info", __VA_ARGS__)
#      ifndef KFS_LOG_INFO
#        undef kfs_debug
#        define kfs_debug(...) kfs_log("debug", __VA_ARGS__)
         /* More explicit logging (for all levels). */
#        undef kfs_log
#        define kfs_log(level, fmt, ...) fprintf(stderr, "[%s] %s:%d %s(): " \
                fmt "\n", "kfs_" level, __FILE__, __LINE__, \
                __func__, ## __VA_ARGS__)
#      endif
#    endif
#  endif
#endif

#endif /* _KFS_LOGGING_H */
