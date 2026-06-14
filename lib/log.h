#pragma once

/*
 * log.h - structured logging macros
 *
 * Debug logging can be toggled with -DATRIUM_DEBUG.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* Set the prefix for every log line (default: "atrium"; max 31 chars). */
void log_set_prefix(const char *prefix);

/* Internal: used only by the _log macro below. */
extern const char *_log_prefix;

/* Internal helper macro. The prio prefix (e.g. "<3>") is parsed by the systemd
journal to set the PRIORITY field; it is stripped from the message. */
#define _log(prio, level, fmt, ...)                                                   \
    do {                                                                              \
        fprintf(stderr, prio "%s [" level "] " fmt "\n", _log_prefix, ##__VA_ARGS__); \
    } while (0)

#define log_info(...)  _log("<6>", "info", __VA_ARGS__)
#define log_warn(...)  _log("<4>", "warning", __VA_ARGS__)
#define log_error(...) _log("<3>", "error", __VA_ARGS__)

/* Replacement for perror(): appends a description of errno. */
#define log_syserr(fmt, ...) _log("<3>", "error", fmt ": %s", ##__VA_ARGS__, strerror(errno))

/* Debug messages (compile-time conditional). */
#ifdef ATRIUM_DEBUG
#define log_debug(...) _log("<7>", "debug", __VA_ARGS__)
#else
#define log_debug(...) ((void)0)
#endif
