#pragma once

/*
 * conf_helpers.h - helpers for parsing config file values.
 *
 * In all functions:
 *   prefix - log message prefix
 *   key    - config key name (used in log messages)
 *   val    - raw string value
 */

#include <stddef.h>

/* Parse a non-negative integer from val into *out, with max the inclusive upper
 * bound. Returns 1 on success, 0 on failure. */
int conf_parse_int(const char *prefix, const char *key, const char *val, long max, int *out);

/* Parse a boolean from val into *out. Accepts "true"/"false", "yes"/"no",
 * "1"/"0", "on"/"off" (case-sensitive). Returns 1 on success, 0 on failure. */
int conf_parse_bool(const char *prefix, const char *key, const char *val, int *out);

/* Copy val into dst (max size dst_size), logging a warning if truncated. */
void conf_copy_str(const char *prefix, const char *key, const char *val, char *dst,
                   size_t dst_size);

/* Append val to a string array (list[max][item_size]). */
void conf_append_strlist(const char *prefix, const char *key, const char *val, char *list,
                         int *count, int max, size_t item_size);
