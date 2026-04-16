#pragma once

#include "seat.h"

/*
 * greeter.h — greeter lifecycle and IPC pipe management.
 *
 * Two anonymous pipes connect the daemon to the greeter inside cage:
 *
 *   credentials pipe  (greeter → daemon):  "<username>\0<password>\0"
 *   result pipe       (daemon → greeter):  "ok\n" | "fail:<reason>\n"
 */

/* Launch a greeter session with IPC pipes.
 * On success s->credentials_rfd and s->result_wfd are populated.
 * Returns 0 on success, -1 on error. */
int greeter_start(struct seat *s);

/* Close the daemon-side IPC pipe ends and reset to -1.
 * The caller must remove credentials_rfd from the event loop first. */
void greeter_stop(struct seat *s);

/* Write a result message ("ok\n" or "fail:<reason>\n") to the greeter.
 * Returns 0 on success, -1 on error. */
int greeter_send_result(struct seat *s, const char *msg);
