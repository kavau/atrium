#pragma once

#include <stdio.h>

/*
 * log.h — Structured logging macros
 *
 * Provides consistent logging with automatic "[prefix] [level] " prefix and
 * newline handling.  The prefix defaults to "atrium" but can be overridden
 * at compile time with -DLOG_PREFIX="name" so that both the daemon and the
 * greeter share the same macros with distinct identification.
 *
 * Debug logging is compile-time togglable via -DATRIUM_DEBUG.
 */

#ifndef LOG_PREFIX
#define LOG_PREFIX "atrium"
#endif

/* Internal helper macro for consistent formatting. */
#define _log(level, ...) \
    do { \
        fprintf(stderr, LOG_PREFIX " [" level "] " __VA_ARGS__); \
        fprintf(stderr, "\n"); \
    } while (0)

/* Informational messages (startup, shutdown, configuration). */
#define log_info(...) _log("info", __VA_ARGS__)

/* Warnings (non-fatal issues, fallback actions taken). */
#define log_warn(...) _log("warn", __VA_ARGS__)

/* Errors (failures, but execution may continue). */
#define log_error(...) _log("error", __VA_ARGS__)

/* Debug messages (compile-time conditional). */
#ifdef ATRIUM_DEBUG
#define log_debug(...) _log("debug", __VA_ARGS__)
#else
#define log_debug(...) ((void)0)
#endif
