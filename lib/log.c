#include "log.h"

#include <stdio.h>

static char _prefix_buf[32] = "atrium";
const char *_log_prefix = _prefix_buf;

void log_set_prefix(const char *prefix) {
    snprintf(_prefix_buf, sizeof(_prefix_buf), "%s", prefix);
}
