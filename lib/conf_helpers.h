#pragma once

/*
 * conf_helpers.h - helpers for reading config files and parsing their values.
 */

#include <stdbool.h>
#include <stddef.h>

/* Look up a key in a simple configuration file and return its value as an
allocated string, or NULL when the file or the key is absent, or when the value
is empty. Handles both "key value" and "key=value".
The caller must free the result. */
char *conf_file_lookup(const char *path, const char *key);

/* Read a file into a NUL-terminated buffer. Returns NULL on failure with errno
set. The caller must free the result. */
char *conf_read_file(const char *path);

/* Returns true if the section header names the given seat. seat can be NULL or
empty, in which case no section matches. */
bool conf_is_seat_section(const char *section, const char *seat);

/* The following functions parse an already-extracted value. In all of them:
     prefix - log message prefix
     key    - config key name (used in log messages)
     val    - raw string value */

/* Parse a non-negative integer from val into *out, with max the inclusive upper
bound. Returns 1 on success, 0 on failure. */
int conf_parse_int(const char *prefix, const char *key, const char *val, long max, int *out);

/* Parse a boolean from val into *out. Accepts "true"/"false", "yes"/"no",
"1"/"0", "on"/"off" (case-sensitive). Returns 1 on success, 0 on failure. */
int conf_parse_bool(const char *prefix, const char *key, const char *val, int *out);

/* Copy val into dst (max size dst_size), logging a warning if truncated. */
void conf_copy_str(const char *prefix, const char *key, const char *val, char *dst,
                   size_t dst_size);

/* Append val to a string array (list[max][item_size]). */
void conf_append_strlist(const char *prefix, const char *key, const char *val, char *list,
                         int *count, int max, size_t item_size);
