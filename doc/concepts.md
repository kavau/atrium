---
Created: April 13, 2026
Last Updated: April 13, 2026
---

# atrium — Concepts

This document explains the Linux systems programming concepts used in each
phase of atrium's implementation. It assumes familiarity with C but not with
Linux-specific APIs.

---

## Phase 1 — Event Loop Foundation

### Signals

A *signal* is an asynchronous notification the kernel (or another process) can
deliver to a process at any time. The process has no say in when it arrives —
it interrupts normal execution the moment the kernel decides to deliver it.

Each signal has a number and a conventional meaning:

| Signal | Meaning |
|---|---|
| `SIGTERM` | Polite request to terminate. The default action is to kill the process, but it can be caught to allow a clean shutdown. |
| `SIGCHLD` | A child process changed state (exited, was stopped, etc.). The kernel sends this to the parent. |
| `SIGKILL` | Forcible termination. Cannot be caught or ignored. |

For a daemon, `SIGTERM` and `SIGCHLD` are the two signals that matter most.
`SIGTERM` is what `systemctl stop` sends; `SIGCHLD` is how the daemon learns
that a child process (greeter, compositor) has exited.

#### The classic signal handler problem

The traditional way to handle signals is to register a handler function with
`sigaction()`. The problem: a signal can arrive at *any* point during execution
— including in the middle of a `malloc()`, `printf()`, or any other function
that uses internal locks or global state. Calling such functions from inside a
signal handler causes deadlocks or corruption.

The POSIX standard defines a whitelist of *async-signal-safe* functions that
are safe to call from a handler. The list is short and does not include most
useful functions (`fprintf`, `malloc`, etc.). Writing correct traditional signal
handlers is consequently fragile and easy to get wrong.

atrium avoids the problem entirely by using `signalfd`.

---

### Blocking signals: `sigprocmask`

Before explaining `signalfd`, it's necessary to understand signal blocking.

Every process has a *signal mask* — a set of signals that are currently
*blocked*. A blocked signal is not ignored; it is held in a *pending* state by
the kernel and delivered only when the signal is unblocked. This is distinct
from `SIG_IGN` (which discards the signal permanently).

`sigprocmask` modifies the signal mask:

```c
sigset_t mask;
sigemptyset(&mask);
sigaddset(&mask, SIGTERM);
sigaddset(&mask, SIGCHLD);
sigprocmask(SIG_BLOCK, &mask, NULL);
```

This tells the kernel: "hold `SIGTERM` and `SIGCHLD` as pending instead of
delivering them asynchronously." After this call, neither signal will interrupt
normal execution.

**Why block before creating `signalfd`?** There is a race window between program
start and the `signalfd` call. If a `SIGTERM` arrives in that window and signals
are not yet blocked, the default action (kill the process) would fire before we
have had a chance to install our handler. Blocking the signal first closes that
window.

---

### `signalfd`

`signalfd` is a Linux-specific mechanism that converts signal delivery into
readable file descriptor events. Instead of a signal handler being called
asynchronously, the signal is queued and can be read like data from a file.

```c
int sfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
```

`-1` means "create a new fd" (passing an existing fd updates it). `mask` is the
same set of signals we blocked with `sigprocmask`. The two must match: blocking
prevents async delivery; `signalfd` provides the synchronous alternative.

When a blocked signal arrives, the kernel makes `sfd` readable. Reading it
yields one or more `struct signalfd_siginfo` structures, one per pending signal.
The field we care about is `ssi_signo`, which is the signal number.

```c
struct signalfd_siginfo si;
read(sfd, &si, sizeof(si));
switch (si.ssi_signo) {
    case SIGTERM: /* ... */ break;
    case SIGCHLD: /* ... */ break;
}
```

This is safe to do from normal code — no async-signal-safety restrictions apply.
Signal handling becomes just another branch of the event loop.

#### `SFD_CLOEXEC`

When the daemon later forks and execs a child process (greeter, compositor),
the child inherits all open file descriptors unless they are marked
*close-on-exec*. The child has no use for the signalfd and shouldn't see it.
`SFD_CLOEXEC` sets the `O_CLOEXEC` flag atomically at creation, so the kernel
closes the fd in the child automatically before the exec'd program starts.

This applies to every fd we create in atrium. The convention is: always
create fds with `O_CLOEXEC` (or the equivalent `_CLOEXEC` flag) unless you
explicitly intend the child to inherit it.

#### `SFD_NONBLOCK`

Marks the fd non-blocking so that `read()` returns immediately with `EAGAIN` if
there is nothing to read, rather than blocking. In practice our event loop only
calls `read()` after `poll()` has confirmed the fd is readable, so `EAGAIN` should
never occur — but non-blocking is defensive practice.

---

### `poll()` and the event loop

`poll()` is the core of the event loop. It takes an array of file descriptors to
watch, blocks until at least one of them is ready, and returns which ones fired.

```c
struct pollfd fds[N];
fds[0].fd     = sfd;
fds[0].events = POLLIN;   /* watch for readability */

int n = poll(fds, N, -1); /* -1 = block forever */
if (fds[0].revents & POLLIN) {
    /* sfd is readable */
}
```

The `struct pollfd` has three fields:
- `fd` — the file descriptor to watch.
- `events` — a bitmask of events you're interested in. `POLLIN` means "notify me when there is data to read."
- `revents` — filled in by the kernel on return, indicating what actually happened.

`POLLIN` fires when a `read()` call on the fd would succeed without blocking.
For a `signalfd` this means a signal is pending. For a pipe read-end, it means
data has been written. For a socket, a message has arrived. The event loop
treats all of these uniformly: something is ready, call the associated callback.

The timeout value `-1` means block indefinitely until at least one fd is ready.
A value of `0` would return immediately (useful for polling); a positive value
is a millisecond timeout.

#### `EINTR`

`poll()` can return `-1` with `errno == EINTR` if it was interrupted by a signal
before any fd became ready. In atrium, with `SIGTERM` and `SIGCHLD` blocked via
`sigprocmask`, this should not occur in normal operation. We handle it anyway
with a `continue` (retry the `poll()`) because it is harmless and defensive.

---

### The single-threaded event loop pattern

atrium's daemon is deliberately single-threaded. All events — signals, udev
hotplug, D-Bus replies, pipe data from greeters — are serialised through the
same `poll()` loop.

The alternative would be to give each subsystem its own thread and use mutexes
to protect shared state. For a daemon with the complexity of atrium this would
cost more than it gains:

- **No locking required.** All state transitions happen in a single thread, so
  there are no races and no need for mutexes.
- **Predictable dispatch order.** When two events arrive simultaneously, `poll()`
  returns both in one call and they are dispatched sequentially. There is no
  question of which fires first or whether they interleave.
- **Easy to reason about.** Reading the code, you can always tell what state the
  system is in at any point in the event loop — there is no concurrent mutation
  to track.

The cost is that a slow callback blocks the entire loop. In practice, atrium's
callbacks do very little work: they read a small amount of data and update some
state. The only potentially slow operation is PAM authentication (Phase 7),
which is noted as a known shortcut.
