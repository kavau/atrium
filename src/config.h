#pragma once

/*
 * config.h — compile-time configuration
 *
 * Runtime-configurable daemon values (compositor, desktop, greeter,
 * restart-delay, seat-enum-delay, ignore-seat) live in /etc/atrium.conf and
 * are loaded via config_file.h.  Greeter-specific values (blank-timeout,
 * cursor settings, font size) live in /etc/atrium-greeter.conf.
 *
 * SHORTCUT: CONFIG_PASSWORDLESS_USERS is still hardcoded here and should
 * eventually move to /etc/atrium.conf (daemon side) and /etc/atrium-greeter.conf
 * (greeter side), or be replaced by group membership checks.
 */

/* Credential protocol limits.
 * MAX_USERNAME_LEN matches POSIX LOGIN_NAME_MAX (255 bytes including NUL).
 * MAX_PASSWORD_LEN follows common PAM convention (512 bytes including NUL).
 * These limits are enforced in both the greeter and daemon to prevent
 * buffer overruns and ensure atomic pipe writes under PIPE_BUF (4096). */
#define CONFIG_MAX_USERNAME_LEN  255
#define CONFIG_MAX_PASSWORD_LEN  512

/* SHORTCUT: Array of usernames that can log in without a password.
 * For these users the greeter skips the password screen and sends empty
 * credentials; the daemon skips PAM authentication and resolves uid/gid
 * directly via getpwnam().  No PAM session is opened (no loginuid, no
 * pam_limits, no keyring) — this is a development convenience, not a
 * production feature.  Set to { NULL } to require passwords for all users. */
#define CONFIG_PASSWORDLESS_USERS  { NULL }

/* Greeter system account (uid < 1000, no home directory).
 * To change the username from the default "atriumdm", update both:
 *   - CONFIG_GREETER_USER below
 *   - GREETER_USER in tools/create-greeter-user.sh */
#define CONFIG_GREETER_USER "atriumdm"

