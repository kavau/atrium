# atrium - Architecture

## Starting a Session

### CreateSession

TODO: we only concern ourselves with non-seat0 seats for now. seat0 is special
due to its associated VT.

Before starting a user session, the display manager must call the the logind
`CreateSession` IPC via the D-Bus. Among other initialization tasks, this will
grant the session access to the seat's input/output devices.

According to the [logind
documentation](https://www.freedesktop.org/software/systemd/man/latest/org.freedesktop.login1.html),
`CreateSession` should not be called directly, but is the job of PAM -
specifically its `pam_systemd` module. Going through PAM simplifies our code
significantly, since no direct D-Bus communication is necessary. There are a few
imporant caveats, however:

- The PAM stack (to be precise, `pam_loginuid`) must not be executed in the
  daemon process, but in a child process (the session helper). It sets
  process-scoped session parameters (in particular `loginuid`) which must not
  leak into the daemon.

- `pam_systemd` uses the PID of the calling process as the session leader, which
  in our case will be the session helper. Hence the session helper must be kept
  alive for the duration of the session.

- Seat, session type, and session class need to be provided explicitly in the
  PAM environment via `pam_putenv()`.

### PAM Authentication

Authentication is handled via
[PAM](https://man7.org/linux/man-pages/man8/pam.8.html) (Pluggable Authenticatin
Modules). The PAM conversation is driven by the session helper and involves four
stages:

1. `pam_start` - initializes the PAM transaction
2. `pam_authenticate` - authenticate the user (via password)
3. `pam_acct_mgmt` - determine if the user's account is valid
4. `pam_setcred` - establish additional credentials for the user
5. `pam_opensession` - activate the user session, call logind `CreateSession`

After the user session completes, we call `pam_close_session` followed by
`pam_setcred(PAM_DELETE_CRED)` and `pam_end`.

### Compositor Launch

Session creation involves asynchronous processes, hence we must wait until the
logind session is fully activated before starting the compositor. This is done
over the D-Bus via `sd_session_is_active(session_id)`.

After the logind session is confirmed to be active, we fork a child process that
will eventually become the user's login shell running the compositor. Within the
child process, there are a few important steps that need to be taken care of
first, however:

1. **Privilege drop**: set the child's `uid`, `gid`, and supplementary groups to
   the corresponding values of the user. This is the most critical step -
   omitting it would instantly grant the user root privileges.

2. **Home directory**: we need to explicitly set the working directory of the
   child process to the user's home directory.

3. **Login shell**: the compositor must be executed within the user's login shell,
   so user-specific configuration files (`~/.profile`) are loaded and PATH is
   configured correctly.
