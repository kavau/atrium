#pragma once

/*
 * greeter/ui.h — greeter UI interface.
 *
 * Declares the single entry point that main.c calls.  The implementation lives
 * in a separate toolkit-specific file (ui-gtk4.c) so the toolkit can be
 * swapped by replacing that one file and updating the meson.build source list
 * — no changes to main.c or the daemon are required.
 *
 * greeter_run_ui() must block until the user dismisses the greeter (clicks OK
 * or equivalent), then return.  The caller (main) exits 0 immediately after,
 * which signals the daemon that the greeter completed successfully.
 */

#include "users.h"

/*
 * greeter_run_ui() — display the login prompt and block until dismissed.
 *
 * users / user_count  array of eligible login users, enumerated by the caller.
 *
 * credentials_fd  write-end of the credential pipe.  On submit, the UI writes
 *                 "<username>\0<password>\0" here.  Pass -1 for standalone dev
 *                 runs (no daemon): the button then quits the app directly.
 *
 * result_fd       read-end of the result pipe.  The daemon replies "ok\n" on
 *                 success or "fail:<reason>\n" on failure.  Must be -1 when
 *                 credentials_fd is -1.
 */
void greeter_run_ui(const greeter_user *users, int user_count,
                    int credentials_fd, int result_fd);
