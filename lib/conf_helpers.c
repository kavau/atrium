#include "conf_helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

int conf_parse_int(const char *prefix, const char *key, const char *val, long max, int *out) {
    char *end;
    long v = strtol(val, &end, 10);
    if (*end != '\0' || v < 0) {
        log_warn("%s: invalid value for '%s': '%s', using default", prefix, key, val);
        return 0;
    }
    if (v > max) {
        log_warn("%s: value for '%s' (%ld) exceeds maximum (%ld), clamping", prefix, key, v, max);
        v = max;
    }
    *out = (int)v;
    return 1;
}

int conf_parse_bool(const char *prefix, const char *key, const char *val, int *out) {
    if (strcmp(val, "true") == 0 || strcmp(val, "yes") == 0 || strcmp(val, "1") == 0 ||
        strcmp(val, "on") == 0) {
        *out = 1;
        return 1;
    }
    if (strcmp(val, "false") == 0 || strcmp(val, "no") == 0 || strcmp(val, "0") == 0 ||
        strcmp(val, "off") == 0) {
        *out = 0;
        return 1;
    }
    log_warn("%s: invalid boolean value for '%s': '%s', using default", prefix, key, val);
    return 0;
}

void conf_append_strlist(const char *prefix, const char *key, const char *val, char *list,
                         int *count, int max, size_t item_size) {
    if (*count >= max) {
        log_warn("%s: too many '%s' entries, ignoring '%s'", prefix, key, val);
        return;
    }
    size_t vlen = strlen(val);
    if (vlen >= item_size)
        log_warn("%s: '%s' value too long, truncating", prefix, key);
    snprintf(list + (size_t)(*count) * item_size, item_size, "%s", val);
    (*count)++;
}

void conf_copy_str(const char *prefix, const char *key, const char *val, char *dst,
                   size_t dst_size) {
    size_t vlen = strlen(val);
    if (vlen >= dst_size)
        log_warn("%s: value for '%s' too long (%zu bytes, max %zu), truncating", prefix, key, vlen,
                 dst_size - 1);
    snprintf(dst, dst_size, "%s", val);
}
