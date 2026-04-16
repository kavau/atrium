---
Created: April 13, 2026
Last Updated: April 16, 2026
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

---

## Phase 2 — Seat Discovery

### D-Bus

D-Bus is a message-passing IPC system that is ubiquitous on Linux desktops and
servers. It provides a structured way for processes to call methods on each
other and subscribe to event notifications (signals), without either side
needing to know the other's process ID or manage raw sockets.

There are two standard buses:

- **System bus** — a single, shared bus for the whole machine. System services
  like logind, udev, and NetworkManager live here. Accessible at
  `/run/dbus/system_bus_socket`. Requires appropriate D-Bus policy to use.
- **Session bus** — one per user login session. Desktop applications use it.
  atrium only ever uses the system bus.

Every entity on D-Bus is addressed by three coordinates:

| Coordinate | Example | Meaning |
|---|---|---|
| Bus name | `org.freedesktop.login1` | The well-known name of the service |
| Object path | `/org/freedesktop/login1` | A specific object within the service |
| Interface | `org.freedesktop.login1.Manager` | A named group of methods and signals on that object |

To call a method, you address it by all three, plus the method name:
`org.freedesktop.login1` / `/org/freedesktop/login1` /
`org.freedesktop.login1.Manager` / `ListSeats`.

---

### logind and the seat model

`systemd-logind` is the session and seat manager in systemd-based Linux
systems. It tracks:

- **Seats** — collections of hardware devices (display, keyboard, mouse) that
  form one workstation. `seat0` is the default seat; additional seats require
  explicit device assignment.
- **Sessions** — an active login by a user on a seat, with associated VT,
  PAM session, and cgroup.
- **Users** — aggregates of all sessions belonging to one UID.

logind is the authoritative source for seat information. It monitors udev
internally and decides which devices belong to which seat. The correct way to
enumerate seats is to ask logind — not to scan udev directly — because logind
may know about seats that have no currently attached devices.

`ListSeats()` on `org.freedesktop.login1.Manager` returns the current list of
seats. atrium calls this once at startup to populate its seat list.

---

### D-Bus type signatures

D-Bus messages are strongly typed. Every method argument and return value has a
type declared in a *signature string* using single-character type codes:

| Code | Type |
|---|---|
| `s` | UTF-8 string |
| `o` | Object path (a string constrained to D-Bus path syntax) |
| `u` | Unsigned 32-bit integer |
| `b` | Boolean |
| `a` | Array (followed by the element type) |
| `(...)` | Struct (fields listed inside the parentheses) |

The `ListSeats()` reply has type `a(so)`: an array of structs, each containing
a string (seat ID, e.g. `"seat0"`) and an object path (the logind object for
that seat, e.g. `/org/freedesktop/login1/seat/seat0`).

Parsing this with sd-bus requires entering and exiting containers explicitly:

```c
/* Enter the outer array. */
sd_bus_message_enter_container(reply, 'a', "(so)");

/* Iterate: enter each struct, read its fields, exit. */
while (sd_bus_message_enter_container(reply, 'r', "so") > 0) {
    const char *seat_id, *object_path;
    sd_bus_message_read(reply, "so", &seat_id, &object_path);
    sd_bus_message_exit_container(reply);
}
sd_bus_message_exit_container(reply); /* exit the array */
```

The `'r'` container type stands for *record* (struct). The distinction between
`'a'` (array) and `'r'` (struct) matches the D-Bus wire protocol — arrays are
homogeneous and length-prefixed; structs are heterogeneous and fixed-layout.

---

## Phase 3 — D-Bus / logind Interface

### sd-bus API patterns

`sd-bus` is the D-Bus client library built into `libsystemd`. It is lower-level
than GDBus or QtDBus but has no dependencies beyond glibc and is the natural
choice for a daemon that already links `libsystemd`.

**Opening the system bus:**

```c
sd_bus *bus;
sd_bus_open_system(&bus);
```

This opens a connection to the system bus socket, performs the D-Bus
authentication handshake, and sends the mandatory `Hello` message that assigns
the connection its unique bus name (e.g. `:1.42`).

**Making a synchronous method call:**

```c
sd_bus_error error = SD_BUS_ERROR_NULL;
sd_bus_message *reply = NULL;
sd_bus_call_method(bus,
    "org.freedesktop.login1",       /* destination */
    "/org/freedesktop/login1",       /* object path */
    "org.freedesktop.login1.Manager", /* interface */
    "ListSeats",                     /* method */
    &error, &reply, "");             /* error out, reply out, arg signature */
```

`sd_bus_call_method` sends the request and blocks internally (without blocking
the caller's fd) until the reply arrives. `error` and `reply` are mutually
exclusive outputs: on failure, `error` is populated and `reply` is `NULL`; on
success, `reply` holds the message and `error` is unset.

Atrium uses synchronous calls during startup (before the event loop starts)
because there is nothing else to do while waiting. Later phases use async calls
via `sd_bus_call_async()` so that the event loop remains responsive.

---

### Reference counting in sd-bus

sd-bus objects (`sd_bus`, `sd_bus_message`, etc.) are reference-counted. The
allocation functions return an object with a reference count of 1. The `_unref`
functions decrement the count; when it reaches zero the object is freed and its
resources released.

```c
sd_bus_message_unref(reply);  /* decrement; frees if count reaches 0 */
sd_bus_unref(bus);            /* flushes pending output, closes socket, frees */
```

All `_unref` functions accept `NULL` and treat it as a no-op, matching the
convention of `free(NULL)`. This allows unconditional cleanup calls regardless
of whether the object was successfully created:

```c
sd_bus_error_free(&error);    /* no-op if error was never set */
sd_bus_message_unref(reply);  /* no-op if reply is NULL */
```

`sd_bus_error` is not reference-counted — it is a plain struct initialised to
`SD_BUS_ERROR_NULL`. `sd_bus_error_free()` releases any strings it holds.

---

### `sd_bus_process()` and message dispatch

Registering the sd-bus fd with the event loop means `poll()` wakes us when the
bus has data to read. But `read()`ing the fd directly is not how sd-bus works —
you call `sd_bus_process()` instead, which drives the internal state machine:

1. Reads pending data from the bus fd into an internal buffer.
2. Parses one complete message from that buffer.
3. Dispatches it — either handling it internally (protocol bookkeeping, pending
   reply matching) or invoking a registered handler.
4. Returns `> 0` if a message was processed, `0` if the buffer is now empty,
   or `< 0` on error.

Because a single `read()` from the kernel may deliver multiple messages into
the buffer, the correct pattern is to drain completely before returning to
`poll()`:

```c
int r;
do {
    r = sd_bus_process(bus, NULL);
} while (r > 0);
```

Returning to `poll()` with messages still buffered would leave them unprocessed
until the next kernel-level read event, which could be arbitrarily delayed.

**Registered handlers** are callbacks attached to specific message patterns:
- `sd_bus_call_async()` registers a reply handler keyed by the outgoing
  message's serial number.
- `sd_bus_add_match()` registers a signal handler for a D-Bus match rule (e.g.
  `"type='signal',member='SeatNew'"`), and tells the bus daemon to route
  matching signals to this connection.

In the current phase none of these are registered yet, so `sd_bus_process()`
handles all traffic internally. Signal handlers and async reply handlers are
introduced in later phases.

---

## Phase 4 — VT Allocation

### Virtual Terminals

A *virtual terminal* (VT) is a kernel abstraction that lets multiple independent
text or graphical sessions share one physical display and keyboard. The kernel
maintains up to 63 VT slots (on a typical configuration). At any moment exactly
one VT is *active* — its output appears on the screen and keyboard input goes to
it. The user switches between VTs with `Ctrl-Alt-F1`, `Ctrl-Alt-F2`, etc., or
programmatically via ioctls.

VTs exist only on `seat0`. Other seats own their display and input devices
directly and are always active; there is nothing to switch between.

Each VT corresponds to a device node `/dev/ttyN` (where N is 1-based). VT1 is
`/dev/tty1`, VT7 is `/dev/tty7`, and so on.

`fgconsole` prints the number of the currently active VT.
`fgconsole --next-available` prints the lowest VT number the kernel considers
unallocated, which is what `VT_OPENQRY` returns.

---

### `ioctl()`

`ioctl()` (*input/output control*) is the general-purpose escape hatch in the
Linux system-call interface. When a device or kernel subsystem needs an
operation that doesn't fit the standard `read`/`write`/`seek` model, it is
exposed as an `ioctl` request:

```c
int ioctl(int fd, unsigned long request, ...);
```

`fd` is an open file descriptor for the relevant device. `request` is a numeric
constant (conventionally defined in a kernel header such as `<linux/vt.h>`) that
identifies the operation. The optional third argument is either an integer value
or a pointer to a struct, depending on the request.

Return value: 0 (or a non-negative result) on success, -1 with `errno` set on
error — same convention as other syscalls.

VT management is entirely ioctl-based. The relevant header is `<linux/vt.h>`.

---

### `VT_OPENQRY` and VT allocation

The kernel tracks each VT slot with a *virtual console struct* (`vc_cons[n].d`).
A slot is *allocated* when that struct is non-NULL — i.e. the kernel has
initialised a virtual console object for it. A slot is *free* when it is NULL.

`VT_OPENQRY` scans the slot array and returns the number of the first free
(NULL) slot:

```c
int vtnr = -1;
ioctl(tty0_fd, VT_OPENQRY, &vtnr);  /* vtnr receives the free slot number */
```

This does *not* allocate the slot by itself — `VT_OPENQRY` only queries. The
slot is allocated lazily by the kernel the first time `/dev/ttyN` is opened.

atrium uses `VT_OPENQRY` solely to discover a free VT number. The VT number is
then passed to logind's `CreateSession()` method (Phase 5), which is the
authoritative binding of a VT to a session. logind tracks this assignment
through the session lifecycle, so there is no need for atrium to hold
`/dev/ttyN` open.

**Historical note:** Early versions of atrium held the VT fd open to "claim"
the slot and prevent races. This approach was abandoned because:
1. logind's `CreateSession()` is the true claim mechanism, not fd ownership.
2. Holding the fd open kept the VT's line discipline active, causing dual
   keyboard input issues (input routed to both the Wayland session and the
   underlying text console).

`VT_DISALLOCATE` reverses the allocation: it frees the virtual console struct,
releasing the slot back to the pool. However, it requires `tty->count == 0` —
no process may have the tty device open. atrium calls `VT_DISALLOCATE` on
shutdown to release the VT number, though in practice the kernel reclaims the
slot automatically when all references are closed.

---

### `/dev/tty0` as a control channel

VT management ioctls (`VT_OPENQRY`, `VT_DISALLOCATE`, `VT_ACTIVATE`, etc.) are
issued against a *VT device fd*, but not necessarily the one you want to
manipulate. The convention is to use **`/dev/tty0`** as the control fd:

- `/dev/tty0` is a special alias for whichever VT is currently active. It does
  not correspond to a specific VT number.
- Opening `/dev/tty0` gives a fd that is valid for issuing VT control ioctls
  regardless of which VT is currently foreground.
- VT management ioctls issued via `/dev/tty0` affect the VT number specified in
  the ioctl argument, not VT0 (which doesn't exist).

Contrast with `/dev/ttyN` (N ≥ 1): opening this gives a fd tied to VT N.
For management ioctls that need to refer to a VT by number, `/dev/tty0` is
simpler because it doesn't require you to already have a specific VT open.

atrium opens `/dev/tty0` for `VT_OPENQRY` and `VT_DISALLOCATE`, then closes it
immediately after. No VT-specific fd is held open — logind tracks the VT
assignment internally once `CreateSession()` has bound the VT to a session.

---

### Controlling terminal and `O_NOCTTY`

Every process belongs to a *process group*, and every process group may have an
associated *controlling terminal* — a tty that delivers job-control signals
(`SIGHUP`, `SIGINT`, `SIGQUIT`) to the group. This is the mechanism that makes
Ctrl-C send `SIGINT` to the foreground process in your shell.

A process acquires a controlling terminal automatically when:
- It is a *session leader* (has called `setsid()` and has no controlling
  terminal yet), and
- It opens a tty device that is not already someone else's controlling terminal.

Daemons call `setsid()` during startup to detach from the parent's session,
which makes them session leaders. If such a daemon then opens `/dev/tty0` or
`/dev/ttyN` without `O_NOCTTY`, the kernel silently assigns that tty as the
daemon's controlling terminal. Any subsequent `SIGHUP` from that tty would kill
the daemon.

`O_NOCTTY` suppresses this behaviour:

```c
open("/dev/tty0", O_RDWR | O_CLOEXEC | O_NOCTTY);
```

With `O_NOCTTY` set, opening the tty never assigns a controlling terminal,
regardless of whether the caller is a session leader. atrium uses `O_NOCTTY` on
every tty `open()` call.

---

### Two-layer kernel TTY state

The `EBUSY` error from `VT_DISALLOCATE` (which may occur during shutdown)
exposes an important distinction: the kernel tracks VT state at two independent
layers.

**Layer 1 — virtual console struct (`vc_cons[n].d`)**
This is what `VT_OPENQRY` and `VT_DISALLOCATE` operate on. It represents whether
the kernel has a live virtual console object for slot N. When all references to
a vc are closed (e.g. if a getty or compositor held `/dev/ttyN` open, then
closed it), the kernel frees this struct and the slot appears free to
`VT_OPENQRY`.

**Layer 2 — tty device open count (`tty->count`)**
This counts how many processes currently have `/dev/ttyN` open (for any reason).
`VT_DISALLOCATE` checks this count and refuses with `EBUSY` if it is non-zero,
to avoid freeing a vc struct that another process is still using.

The two layers can be in seemingly contradictory states:

- `VT_OPENQRY` reports the slot free (vc struct freed).
- `VT_DISALLOCATE` returns `EBUSY` (a process still has the tty device open).

Both are correct. For example, a getty that opened `/dev/tty1` at boot holds a
tty reference even if the associated vc struct has been freed. `VT_DISALLOCATE`
correctly refuses because the tty device is still in use, even though the VT
slot itself is available for reuse by another logind session.

atrium silently ignores `EBUSY` from `VT_DISALLOCATE` because the slot being
available to `VT_OPENQRY` is sufficient — the kernel will reclaim resources as
processes close their tty fds.

---

## Phase 5 — Session Launch

Phase 5 forks a compositor process for each seat, creates a logind session via
D-Bus, and manages the compositor lifecycle. This phase introduces several
fundamental systems programming concepts: process creation with `fork`/`exec`,
inter-process synchronization pipes, privilege dropping, and logind session
management.

### fork and exec

Unix process creation is a two-step operation:

1. **`fork()`** creates an exact copy of the calling process. The parent gets
   the child's PID as the return value; the child gets 0.
2. **`exec()`** (or one of its variants: `execlp`, `execve`, etc.) replaces the
   current process image with a new program. The PID does not change.

After `fork()`, parent and child share the same memory through *copy-on-write*
(COW): the kernel maps the same physical pages into both processes but marks
them read-only. The first write to any page triggers a page fault, and the
kernel creates a private copy for the writer. This means:

- The child *can* read anything the parent had in memory at the time of fork.
- The child *cannot* see writes the parent makes after fork, and vice versa.

This COW property is critical for atrium's design. The parent needs to call
`CreateSession` (which populates session_id, runtime_path, etc.) *after* fork,
but those writes are invisible to the child. Values the child needs must either
be computed before fork or communicated through an explicit IPC mechanism (like
the sync pipe).

#### `_exit()` vs `exit()` in the child

After `fork()`, the child should call `_exit()` (not `exit()`) if it needs to
abort before calling `exec()`. The difference:

- `exit()` runs `atexit` handlers and flushes stdio buffers. Since the child
  inherited the parent's stdio buffers, flushing them would duplicate any
  buffered output.
- `_exit()` terminates immediately without running cleanup handlers.

After a successful `exec()`, this distinction is moot — `exec` replaces the
entire process image including stdio buffers.

#### `_Noreturn`

C11 provides the `_Noreturn` keyword (or `[[noreturn]]` in C23) to tell the
compiler that a function never returns to its caller — it always terminates via
`_exit()`, `exec()`, `abort()`, or an infinite loop. The compiler uses this to:

- Suppress "control reaches end of non-void function" warnings.
- Optimize the caller by not generating code to save/restore registers.
- Warn if the function *could* return (e.g. a missing `_exit()` after a failed
  `exec()`).

atrium uses `_Noreturn` for `child_exec_compositor()`, which either execs the
compositor or calls `_exit()`.

---

### Synchronization pipe

When a parent process needs to coordinate with a forked child, a pipe provides
a simple and reliable mechanism. A pipe is a pair of file descriptors: one for
reading, one for writing. Data written to the write end can be read from the
read end.

atrium uses a *sync pipe* to make the child wait until the parent has finished
setting up the logind session:

```
Parent                              Child
──────                              ─────
fork()                              fork()
close(read_end)                     close(write_end)
CreateSession(child_pid)            read(read_end)  ← blocks
ActivateSession()                       ...
wait_session_active()                   ...
udevadm settle                          ...
close(write_end)  ─── EOF ───→      read returns 0
                                    exec compositor
```

When the parent closes the write end, the child's `read()` returns 0 (EOF).
This is the signal to proceed. If the parent dies unexpectedly, `read()` also
returns 0 (the kernel closes the write end), so the child doesn't hang forever.

#### `pipe2` and `O_CLOEXEC`

`pipe2()` is a Linux extension that creates a pipe and atomically sets flags on
both file descriptors. The critical flag is `O_CLOEXEC` (*close-on-exec*):

```c
int sync_pipe[2];
pipe2(sync_pipe, O_CLOEXEC);
```

Without `O_CLOEXEC`, the pipe fds would be inherited by any process the child
`exec`s, and they would remain open in the compositor process. The compositor
has no use for them, and leaked fds can cause subtle resource exhaustion bugs.

`O_CLOEXEC` tells the kernel to automatically close these fds when `exec()` is
called. The fds are still available in the child between `fork()` and `exec()`
(where they're needed for synchronization), but they do not leak into the
compositor.

The older `pipe()` call does not accept flags. Setting `O_CLOEXEC` after
`pipe()` via `fcntl()` introduces a race window where another thread could
`fork()+exec()` and inherit the unprotected fd. `pipe2()` eliminates this race.

#### `F_DUPFD_CLOEXEC`

Similarly, when duplicating a file descriptor (e.g. to preserve the logind
session fifo fd after `sd_bus_message_unref`), `fcntl(fd, F_DUPFD_CLOEXEC, 0)`
is preferred over `dup(fd)`. The `dup` call creates a new fd without
`O_CLOEXEC`, meaning it would leak into child processes (the compositor, or
even `udevadm settle`). `F_DUPFD_CLOEXEC` atomically duplicates the fd and sets
the close-on-exec flag.

---

### logind sessions and `CreateSession`

logind (part of systemd) manages user sessions. Each session represents a
user's login on a specific seat. logind tracks:

- Which user is logged in on which seat.
- Which VT the session occupies (seat0 only).
- Whether the session is active (foreground) or inactive (background).
- Which cgroup scope the session's processes run in.

#### Session leader PID

`CreateSession` accepts a `pid` parameter specifying the *session leader* — the
process logind considers the main process of the session. This affects:

1. **Cgroup scoping.** logind moves the session leader (and its children) into
   a dedicated cgroup scope (`session-cXX.scope`). If the display manager's PID
   is passed instead of the compositor's, the display manager itself gets moved
   into the session scope, leaving its original service scope. This breaks
   systemd's ability to manage the display manager (e.g. `systemctl stop` may
   not reach it because it's no longer in the expected cgroup).

2. **Process tracking.** logind monitors the session leader to detect session
   end. When the leader exits, logind may clean up the session.

atrium forks the compositor *before* calling `CreateSession`, then passes the
child's PID. This ensures the compositor (not the daemon) is the session leader.

#### Session activation

After creating a session, it must be *activated* to become the foreground
session on its seat. For seat0, this requires an explicit D-Bus call
(`ActivateSession`) which triggers an internal VT switch in logind. Other seats
become active automatically when they have exactly one session.

However, `ActivateSession` is asynchronous — it returns before the session is
fully active. The compositor cannot safely call `TakeDevice` (to open DRM/input
devices) until the session is actually active. atrium polls
`sd_session_is_active()` (which reads `/run/systemd/sessions/<id>` directly,
no D-Bus round-trip) every 20 ms for up to 2 seconds.

#### udevadm settle

Even after the session is active, there is a second race: logind triggers udev
rules to update device ACLs (e.g. granting the session user access to
`/dev/dri/card0`), but these rules execute asynchronously. `udevadm settle`
blocks until the udev event queue is drained, ensuring ACLs are in place before
the compositor opens devices.

#### Session fifo

`CreateSession` returns a file descriptor to a logind-managed FIFO. Keeping
this fd open tells logind the session is alive. Closing it signals session end.
This is the mechanism for clean session teardown — when atrium closes the fifo
(in `session_cleanup`), logind removes the session.

---

### Privilege dropping

atrium runs as root (it needs root to call `CreateSession`, open VTs, etc.).
The compositor should *not* run as root — it runs as the session user.

Privilege dropping is the sequence of system calls that transitions a process
from root to a regular user. The order matters:

```c
initgroups(username, primary_gid);  /* 1. set supplementary groups */
setresgid(gid, gid, gid);          /* 2. set group ID (all three slots) */
setresuid(uid, uid, uid);           /* 3. set user ID (all three slots) */
```

The sequence must be: supplementary groups first, then GID, then UID. Once the
UID is dropped, the process can no longer call `setresgid` or `initgroups`
(those require root privileges).

#### `setresuid` / `setresgid` vs `setuid` / `setgid`

Each process has three user IDs:

| ID | Purpose |
|---|---|
| **Real UID** | The user who owns the process. |
| **Effective UID** | The UID used for permission checks. |
| **Saved set-UID** | A copy preserved across `exec()`. Can be used to re-escalate. |

`setuid(uid)` has complex semantics that depend on whether the process has
`CAP_SETUID`. For a root process, `setuid(uid)` sets all three IDs — but this
behavior is an implementation detail, not a guarantee across all contexts.

`setresuid(ruid, euid, suid)` explicitly sets all three IDs in one call. This
is unambiguous and preferred for security-critical code.

#### Re-escalation check

After dropping privileges, atrium verifies the drop was effective:

```c
if (setresuid(0, 0, 0) == 0) {
    /* CRITICAL: this should fail with EPERM */
    _exit(1);
}
```

If `setresuid(0, 0, 0)` succeeds, the saved set-UID was not properly cleared,
and the process could re-escalate to root. This defence-in-depth check catches
such cases.

---

### Signal mask inheritance

`fork()` inherits the parent's signal mask. `exec()` does *not* reset it. This
means any signals blocked in the parent (e.g. `SIGTERM` and `SIGCHLD` for
`signalfd`) remain blocked in the child after exec.

For a compositor, this is a problem:

- Blocked `SIGTERM` means `kill <compositor>` has no effect (the signal pends
  indefinitely).
- Blocked `SIGCHLD` means the compositor cannot reap its own children (e.g.
  XWayland), leading to zombie processes.

The child must unblock these signals before exec:

```c
sigset_t parent_mask;
sigemptyset(&parent_mask);
sigaddset(&parent_mask, SIGTERM);
sigaddset(&parent_mask, SIGCHLD);
sigprocmask(SIG_UNBLOCK, &parent_mask, NULL);
```

`SIG_UNBLOCK` removes only the specified signals from the mask, preserving any
other mask state. This is slightly more precise than `SIG_SETMASK` with an
empty set, which would clear the entire mask (overwriting any signals that might
have been blocked by other code).

---

### Session lifecycle: stop vs shutdown

atrium distinguishes two paths for ending a session:

**`session_stop()`** — called from the SIGCHLD handler after the child has
already exited and been reaped by `waitpid`. No signal is sent (the compositor
is already dead). The function just closes the logind fifo and clears state.

**`session_shutdown()`** — called during daemon-initiated teardown (e.g. atrium
receives SIGTERM and needs to stop all compositors). The compositor may still be
running, so this function:

1. Sends `SIGTERM` to the compositor.
2. Polls `waitpid(pid, &status, WNOHANG)` every 100 ms for up to 5 seconds.
3. If the compositor still hasn't exited, sends `SIGKILL` (which cannot be
   caught or ignored).

The `WNOHANG` flag makes `waitpid` non-blocking: it returns 0 if the child
hasn't exited yet, positive if the child has been reaped, or -1 with
`errno == ECHILD` if the child was already reaped (e.g. by the SIGCHLD handler).

This timeout-and-escalate pattern is standard in daemon code. Without it, a
compositor that ignores `SIGTERM` would cause the daemon's shutdown to hang
indefinitely.

---

## Phase 6 — Session Environment

Phase 6 closes the gap between "compositor runs" and "compositor runs in a
proper user session environment."

### Login Shell Wrapping

When the daemon forks the compositor, `execlp(compositor, ...)` runs it
directly — as a plain process with the daemon's environment. The user's shell
profile (`.profile`, `.bash_profile`, `.zshenv`/`.zlogin`) never executes,
so customizations like `PATH` additions, locale settings, and tool-specific
environment variables are missing from the entire session.

The fix is to exec the compositor *through* the user's login shell:

```c
execlp(pw->pw_shell, pw->pw_shell, "-l", "-c",
       "exec compositor", (char *)NULL);
```

The `-l` flag tells the shell to behave as a login shell, sourcing the
profile files. The `exec` keyword in the `-c` string replaces the shell
process with the compositor, so there is no extra process in the hierarchy —
`SIGTERM` from the daemon reaches the compositor directly, and `waitpid`
sees the compositor's exit status, not the shell's.

### Desktop Identity Variables

XDG desktop portals use `XDG_CURRENT_DESKTOP` to select the correct portal
backend (e.g. `xdg-desktop-portal-gnome` vs `xdg-desktop-portal-kde`).
Without it, portal requests (file chooser, screen sharing, etc.) may use a
wrong or fallback implementation.

`XDG_SESSION_DESKTOP` serves a similar role for logind and session managers
that inspect the active session type.

### D-Bus Session Bus Address

On systemd systems, `systemd --user` provides a per-user D-Bus bus socket at
`/run/user/<uid>/bus`. Most D-Bus libraries auto-detect this path, but some
applications check `DBUS_SESSION_BUS_ADDRESS` explicitly. Setting it
defensively avoids subtle failures in applications that rely on the variable.

---

## Phase 7 — Standalone Greeter App

### Greeter as a Separate Process

The greeter is a separate binary, not linked into the daemon.  In production
the daemon will exec it inside a cage compositor; during development it can
run standalone in a regular desktop session.  This separation means the
greeter can be built, tested, and iterated on without involving the daemon,
logind, or PAM.

### Pipe-based IPC Protocol

The greeter communicates with the daemon through two unidirectional pipes:

- **Credential pipe** (greeter → daemon): on submit, the greeter writes
  `<username>\0<password>\0` — two null-terminated strings.
- **Result pipe** (daemon → greeter): the daemon replies with either
  `ok\n` (success, greeter exits) or `fail:<reason>\n` (error shown,
  user can retry).

Pipe file descriptors are passed via environment variables rather than
command-line arguments — this avoids exposing them in `/proc/*/cmdline`
and allows the greeter to detect their absence (standalone mode) cleanly.

### GLib Event Integration

GTK4 runs its own main loop (GLib's `GMainLoop`).  The greeter needs to
react to data arriving on the result pipe without blocking the UI.

`g_unix_fd_add()` registers a file descriptor as a GLib event source so
that a callback fires when the fd becomes readable — the same idea as the
daemon's `poll()` loop, but using GLib's infrastructure.  This avoids
spawning a thread or doing a blocking `read()` that would freeze the UI.

---

## Phase 8 — Greeter Integration

Phase 8 wires the standalone greeter into the daemon. The daemon forks cage
hosting `atrium-greeter` on each seat, reads credentials from the pipe, launches
the user's compositor, and restarts the greeter when the compositor exits. This
introduces a seat state machine, deliberate exceptions to the `O_CLOEXEC` rule,
and several defensive patterns around pipe lifecycle and credential handling.

### Seat State Machine

Each seat progresses through three states:

```
SEAT_IDLE  →  SEAT_GREETER  →  SEAT_SESSION
                    ↑                 │
                    └─────────────────┘
```

- **`SEAT_IDLE`** — no process running. Initial state at startup and after
  `session_stop()`.
- **`SEAT_GREETER`** — cage + atrium-greeter is running, credential pipe is
  open. The daemon is waiting for the user to select a username.
- **`SEAT_SESSION`** — the user's compositor is running. The daemon is waiting
  for it to exit.

All state transitions are driven by `SIGCHLD`. When the daemon reaps a child,
it looks up the seat by PID and decides the next action based on the current
state and exit status:

| Previous state | Exit status | Action |
|---|---|---|
| `SEAT_GREETER` | exit(0) | Greeter completed successfully — start compositor (`SEAT_SESSION`) |
| `SEAT_GREETER` | crash/non-zero | Greeter failed — restart greeter after delay |
| `SEAT_SESSION` | any | Compositor exited — restart greeter |

This approach keeps the state machine entirely event-driven: there are no
threads, no polling loops, and no timers involved in the normal flow. The
`SIGCHLD` handler in the daemon's event loop is the single point where all
child-exit events converge.

### Pipes Without `O_CLOEXEC`

atrium's coding conventions require `O_CLOEXEC` on all file descriptors to
prevent fd leaks across `exec` boundaries. The greeter IPC pipes are the one
deliberate exception.

The pipes must survive *two* `exec` calls:

1. daemon `fork()` → child `exec(cage)` — cage inherits the pipe fds
2. cage starts the greeter → `exec(atrium-greeter)` — greeter inherits them

If the pipes were created with `O_CLOEXEC`, the kernel would close them at
the first `exec` and the greeter would never see them. Creating them with
plain `pipe()` (no flags) ensures they propagate through the entire chain.

This is safe because the daemon closes the child-side pipe ends immediately
after fork (see below), so they do not leak into *other* children. Only the
one child that needs them inherits them.

### Fd Numbers and Environment Variables

When a parent forks a child, the child inherits copies of all open file
descriptors — but there is no API for a child to discover *which* fds it has
or what they are for. The parent must communicate the fd numbers explicitly.

atrium uses environment variables:

```c
char cfd_str[16];
snprintf(cfd_str, sizeof(cfd_str), "%d", cr_pipe[1]);
setenv("CREDENTIALS_FD", cfd_str, 1);
```

The `setenv` happens *before* `fork()`, so the child inherits the variable
in its environment. The greeter reads it at startup:

```c
const char *cfd_env = getenv("CREDENTIALS_FD");
int credentials_fd = atoi(cfd_env);
```

The alternative — passing fd numbers as command-line arguments — would expose
them in `/proc/<pid>/cmdline`, which any process on the system can read.
Environment variables are slightly more private (`/proc/<pid>/environ` requires
same-uid or `CAP_SYS_PTRACE`).

### Environment Variable Cleanup After Fork

The `setenv()` / `unsetenv()` pattern has a subtle timing requirement. The
daemon calls `setenv` before fork so the child inherits the variable.
Immediately after fork (in the parent), it calls `unsetenv`:

```c
setenv("CREDENTIALS_FD", cfd_str, 1);
setenv("RESULT_FD",      rfd_str, 1);

int r = session_start_greeter(s);   /* forks internally */

unsetenv("CREDENTIALS_FD");
unsetenv("RESULT_FD");
```

Without the `unsetenv`, the variables would persist in the daemon's
environment and be inherited by *every* subsequent `fork`/`exec` — including
compositor launches. A compositor process would inherit stale pipe fd numbers
pointing at already-closed file descriptors (or worse, recycled fds pointing
at something unrelated).

### Parent-Side Pipe Hygiene

After forking, the daemon immediately closes the pipe ends that belong to
the child:

```c
close(cr_pipe[1]); /* greeter's write end — daemon only reads [0] */
close(re_pipe[0]); /* greeter's read end  — daemon only writes [1] */
```

This is not just for tidiness — it is essential for correct EOF behavior.

A pipe delivers EOF to the reader only when *all* write-end file descriptors
are closed. If the daemon kept `cr_pipe[1]` open, then when the greeter
exits (closing its copy of the write end), the daemon's `read()` on
`cr_pipe[0]` would block forever instead of returning 0 (EOF). The daemon
would never learn that the greeter has gone.

The same logic applies in reverse: the greeter needs EOF on the result pipe
if the daemon crashes. Closing `re_pipe[0]` in the daemon ensures the greeter
is the only holder of the read end.

### Credential Wiping

The credentials pipe carries plaintext passwords (even though they are
currently ignored). As a defensive measure, the daemon wipes the read buffer
immediately after parsing:

```c
memcpy(s->greeter_username, username, ulen + 1);
memset(buf, 0, sizeof(buf));         /* wipe credentials */
```

This limits the window during which the password exists in process memory.
It does not provide strong security — the kernel, a debugger, or a core dump
could still capture the password — but it reduces the exposure surface and
establishes the pattern for when PAM authentication is wired in later.

### User Enumeration with `getpwent()`

The greeter needs a list of local users to display in the user picker. The
POSIX password database API provides this:

```c
setpwent();                      /* rewind to start of database */
while ((pw = getpwent()) != NULL) {
    /* pw->pw_name, pw->pw_uid, pw->pw_gecos, pw->pw_shell */
}
endpwent();                      /* release resources */
```

`getpwent()` iterates over every entry in `/etc/passwd` (and any configured
NSS backends like LDAP). Each call returns a pointer to a `struct passwd`
in static storage — the next call overwrites it, so fields must be copied
out immediately.

The greeter filters the raw list to show only interactive login accounts:

- **UID range:** `uid >= 1000 && uid < 65534` — excludes system accounts
  (uid < 1000) and the `nobody` user (65534).
- **Shell check:** excludes users whose shell contains `nologin` or is
  `/bin/false` — these are service accounts that cannot log in.

The `pw_gecos` field traditionally holds the user's full name (possibly
followed by comma-separated fields like room number and phone). The greeter
uses it as-is for the display name, falling back to the username if GECOS is
empty.

## Phase 9 — Standalone PAM Module

Phase 9 introduces PAM (Pluggable Authentication Modules) — the standard
Linux framework for authenticating users.  atrium's `auth.c` wraps the PAM C
API into three functions (`auth_begin`, `auth_open_session`, `auth_close`)
that the daemon calls at different points in the login lifecycle.

### PAM Architecture

Traditional Unix programs that authenticate users (login, su, sshd) each
contain their own password-checking code.  PAM replaces this with a shared
framework: the application calls a generic "authenticate this user" API, and
PAM delegates to a configurable stack of *modules* that perform the actual
work.

The key abstraction: **the application does not know how authentication
happens**.  PAM reads a service-specific configuration file in `/etc/pam.d/`
(named after the application — `atrium` in our case) and executes the listed
modules in order.  An administrator can change the authentication policy
(require a hardware token, add two-factor, use LDAP) without recompiling
the application.

PAM organises its work into four *management groups*:

| Group       | Purpose                                                    |
|-------------|------------------------------------------------------------|
| `auth`      | Verify the user's identity (password, fingerprint, etc.)   |
| `account`   | Check whether the account is valid (expired, locked, etc.) |
| `session`   | Set up / tear down the login environment (audit, limits)   |
| `password`  | Change the user's authentication token (not used by atrium)|

Each line in a PAM config file specifies one module for one group, with a
*control flag* that determines how failure is handled:

- **`required`** — the module must succeed, but the stack continues to run
  remaining modules (so all are exercised before the final result).
- **`requisite`** — the module must succeed; on failure, return immediately
  without running further modules.
- **`sufficient`** — if this module succeeds (and no prior `required` module
  failed), return success immediately without running further modules.
- **`optional`** — the module's result is ignored unless it is the only module
  in the stack for this group.

The `include` directive (Arch/Fedora) and `@include` directive (Debian) pull
in another file's rules, e.g. `auth include system-auth` pulls in the
system-wide authentication stack.  This is convenient but means the
application inherits every module in the included file — including modules
that may be inappropriate for its context (as we discovered with
`pam_systemd.so` in Phase 10).

### The PAM Conversation Mechanism

PAM's most unusual design decision is the *conversation function*.  Rather
than accepting a password as a direct argument, `pam_authenticate()` calls
back into the application to request credentials:

```c
struct pam_conv conv = {
    .conv        = pam_conv_fn,      /* callback function */
    .appdata_ptr = (void *)password, /* opaque pointer passed to callback */
};
pam_start("atrium", username, &conv, &pamh);
```

When a PAM module needs input (e.g. "Password: "), it sends a message through
the conversation function.  The application responds with the appropriate
credential.  This indirection exists because PAM was designed for interactive
terminals — the conversation function would prompt the user and read their
response.  A display manager like atrium already has the password (received
from the greeter via pipe), so its conversation function simply returns the
pre-collected value.

The callback receives an array of messages, each tagged with a style:

| Style                | Meaning                                    |
|----------------------|--------------------------------------------|
| `PAM_PROMPT_ECHO_OFF`| Request secret input (password)           |
| `PAM_PROMPT_ECHO_ON` | Request visible input (username, OTP)     |
| `PAM_ERROR_MSG`      | Error text from a module (display to user) |
| `PAM_TEXT_INFO`       | Informational text (display to user)      |

For each prompt, the callback allocates a `pam_response` containing the
reply string.  **PAM takes ownership** of both the response array and the
strings inside it — they must be `malloc`'d, not stack-allocated or
string-literal pointers.  PAM calls `free()` on them when it is done.

This ownership model is why `strdup(password)` is used rather than returning
the pointer directly: PAM will `free()` whatever it receives, and freeing a
pointer into a stack buffer or static storage would corrupt the heap.

### The PAM API Call Sequence

A complete PAM session follows a strict order.  Each step depends on the
previous one succeeding:

```
pam_start()          — initialise the PAM library; creates the handle
    │
pam_authenticate()   — verify the password via the "auth" module stack
    │
pam_acct_mgmt()      — check account validity via the "account" stack
    │                   (expiry, lockout, time restrictions)
    │
pam_setcred()        — establish user credentials (Kerberos tickets,
    │                   supplementary group memberships, etc.)
    │
pam_open_session()   — open a login session (audit log, utmp/wtmp,
    │                   kernel keyring, pam_loginuid, resource limits)
    │
pam_getenvlist()     — collect environment variables set by modules
    │                   (e.g. MAIL, KRB5CCNAME)
    │
    ╰── ... session runs ...
    │
pam_close_session()  — tear down the session (close audit record, etc.)
    │
pam_setcred(DELETE)  — destroy credentials created earlier
    │
pam_end()            — release all PAM resources; invalidates the handle
```

Each function returns `PAM_SUCCESS` on success or an error code.
`pam_strerror(pamh, r)` converts error codes to human-readable strings.

On failure at any step, the sequence must be unwound: if `pam_authenticate`
fails, only `pam_end` is needed (no session was opened).  If
`pam_open_session` fails, `pam_setcred(DELETE)` and `pam_end` are needed.
The cleanup must mirror what was successfully set up — skipping a close call
can leak kernel resources (audit records, keyring references).

### `pam_fail_delay` and Brute-Force Protection

`pam_unix` (the standard password module) inserts a random delay (roughly
0–2 seconds) after each failed authentication attempt.  This slows down
brute-force attacks over SSH or at a text console where an attacker can
script rapid retries.  On top of that, many distributions include
`pam_faildelay.so` in their system-auth stack that sets a fixed floor (e.g.
3 seconds on Arch/CachyOS).

The application *can* influence the delay via `pam_fail_delay(pamh, usec)`,
but the actual delay is the maximum of all delays requested by the
application and every module in the stack.  An application can only raise
the floor, never lower it below what the modules set.

atrium does **not** call `pam_fail_delay` at all — it defers entirely to the
system's PAM configuration.  A display manager's greeter UI already
serialises login attempts (the user must type a password and click "Login"),
so the module-level delay simply adds a brief pause before the
"authentication failed" message appears.  Calling `pam_fail_delay(pamh, 0)`
would be pointless at best (modules override it) and a security weakness at
worst (on a minimal PAM config that omits `pam_faildelay.so`, it would
remove the `pam_unix` baseline delay entirely).

### `getpwnam` and the Name Service Switch

After successful authentication, atrium needs the user's numeric uid and gid
to create the logind session and drop privileges.  `getpwnam()` resolves a
username to a `struct passwd`:

```c
struct passwd *pw = getpwnam(username);
/* pw->pw_uid, pw->pw_gid, pw->pw_dir, pw->pw_shell */
```

`getpwnam` returns a pointer to *static storage* — the next call to
`getpwnam` (or `getpwent`) overwrites it.  Any fields needed later must be
copied out immediately.  In atrium's case, we store uid and gid as integers
in `auth_result`, which is safe.

The function returns NULL on both "user not found" and "system error".  To
distinguish the two, clear `errno` before the call:

```c
errno = 0;
struct passwd *pw = getpwnam(username);
if (!pw) {
    if (errno)
        /* hard error: LDAP timeout, NSS misconfiguration, etc. */
    else
        /* user simply does not exist in any database */
}
```

Behind the scenes, `getpwnam` does not just read `/etc/passwd`.  It goes
through the **Name Service Switch** (NSS), configured in `/etc/nsswitch.conf`.
A typical entry:

```
passwd: files systemd
```

This means: first check `/etc/passwd` (the `files` provider), then try
`systemd` (which can synthesise entries for `root` and `nobody`).  On systems
with LDAP or SSSD, additional providers appear here, and `getpwnam` queries
them transparently.  The application never needs to know where the user
database lives.

### PAM Handle Lifecycle

The PAM handle (`pam_handle_t *pamh`) is an opaque structure created by
`pam_start()` and destroyed by `pam_end()`.  It carries all state for the
PAM transaction: which modules are loaded, what credentials were established,
whether a session is open.

A critical design constraint: **the handle must remain live between
`pam_open_session` and `pam_close_session`**.  The session may run for hours
(a user's desktop session), and `pam_close_session` must use the same handle
to properly undo what `pam_open_session` set up (close audit records, remove
kernel keyring links, etc.).

This is why `auth_result` stores the handle:

```c
typedef struct auth_result {
    uid_t         uid;
    gid_t         gid;
    char        **pam_env;
    pam_handle_t *pamh;     /* retained for auth_close() */
} auth_result;
```

`auth_begin()` creates the handle and stores it.  The session runs (possibly
for hours).  When the session ends, `auth_close()` calls `pam_close_session`,
`pam_setcred(DELETE)`, `pam_end`, and frees the environment list — in that
order.  Calling `pam_end` without first closing the session would leak
resources held by session modules.

## Phase 10 — PAM Integration

Phase 10 wires the standalone PAM module (Phase 9) into the daemon's login
flow.  The integration uncovered two critical issues — a logind session
conflict and a VT keyboard security leak — that required splitting the PAM
sequence across processes and taking explicit control of VT keyboard modes.

### Splitting PAM Across `fork()`

The PAM API is designed for a single process that authenticates a user and
then becomes that user's session.  A display manager has a fundamentally
different architecture: a long-lived daemon authenticates many users over its
lifetime, but the actual sessions run in forked child processes.

The solution is to split the PAM call sequence at the `fork()` boundary:

```
Daemon (parent):                  Child (compositor):
  pam_start()
  pam_authenticate()
  pam_acct_mgmt()
  getpwnam()
        │
        ├── fork() ──────────────►  auth_open_session():
        │                             pam_setcred()
        │                             pam_open_session()
        │                             pam_getenvlist()
        │                             ... exec compositor ...
        │
        ╰── (session runs) ──────►  compositor exits
        │
  auth_close():
    pam_close_session()
    pam_setcred(DELETE)
    pam_end()
```

The dividing line is deliberate: `pam_authenticate` and `pam_acct_mgmt` are
pure checks that don't modify kernel state — they can safely run in the
daemon.  `pam_setcred` and `pam_open_session` modify per-process state (audit
records, kernel keyrings, loginuid) that must be associated with the
compositor's PID, not the daemon's.

The `pamh` handle survives the fork because `fork()` duplicates the entire
address space.  The child gets a copy of the handle and uses it for
`pam_setcred` / `pam_open_session` / `pam_getenvlist`.  The parent retains
the original and uses it later for `pam_close_session` / `pam_end`.  The two
copies share no state after the fork — this is safe because PAM modules store
all state either in the handle (process-local memory) or in the kernel
(per-PID audit records, etc.).

A subtlety: `pam_close_session` runs in the daemon (parent), not the child.
At that point the child has already exited, so the daemon calls
`pam_close_session` on its copy of the handle.  Session modules that need to
undo per-process state (like removing kernel keyring links) cannot do so
from the parent — but in practice the kernel cleans up process-scoped
resources automatically when the child exits.  The modules that *do* need
explicit close (like writing utmp/wtmp logout records) operate on
system-wide databases, not per-process state, so running in the parent is
fine.

### `/proc/self/loginuid` and the Audit Subsystem

The Linux audit subsystem (used by `auditd` and tools like `ausearch`)
tracks which user originally initiated a login session.  This identity is
stored in `/proc/self/loginuid` — a per-process value that `pam_loginuid.so`
writes during `pam_open_session`.

The critical property of loginuid: **it is a one-shot value**.  Once set (the
initial value is `-1` / `4294967295`, meaning "unset"), it is locked.  On
most modern kernels, even root cannot change it after it has been set
(`/proc/sys/kernel/loginuid_immutable` or compiled-in policy).

This has profound implications for a display manager:

- If `pam_loginuid` runs in the **daemon**, the daemon's loginuid gets set to
  the first user who logs in — permanently.  Every subsequent session for any
  user would inherit the wrong loginuid, breaking audit trails.

- If `pam_loginuid` runs in the **child** (after fork, before exec), it
  correctly sets the compositor's loginuid to the authenticating user.  Each
  child gets its own `/proc/self/loginuid`, and the daemon's stays at `-1`.

This is one of the primary reasons the PAM split exists.  The pattern — "run
session modules in the child's PID context" — is the standard approach used
by greetd, sddm, and lightdm.

### `pam_systemd.so` and the logind Session Conflict

`pam_systemd.so` is the PAM module that integrates login sessions with
systemd-logind.  When `pam_open_session` invokes it, it calls logind's
`CreateSession` D-Bus method for the calling process's PID — effectively
telling logind "this PID is a new user session."

A display manager like atrium already manages logind sessions explicitly: it
forks the child, passes the child's PID to `CreateSession` via D-Bus, and
activates the resulting session.  If `pam_systemd.so` *also* runs during
`pam_open_session`, one of two things happens depending on where the PAM
session is opened:

1. **`pam_open_session` in the daemon:** `pam_systemd` creates a logind
   session scoped to the daemon's PID.  The daemon is now "inside a session."
   The subsequent explicit `CreateSession` for the compositor child fails:
   ```
   CreateSession: Already running in a session or user slice
   ```
   logind refuses to create a second session for a process that already
   belongs to one (the child inherits the daemon's cgroup scope after fork).

2. **`pam_open_session` in the child:** `pam_systemd` creates a session for
   the child's PID.  But atrium has already called `CreateSession` for that
   same PID moments earlier (to get the session ID and runtime directory).
   This would create a *second* logind session for the same PID — at best
   redundant, at worst confusing.

The clean solution: **exclude `pam_systemd.so` from the PAM stack entirely**.
atrium manages logind sessions directly via D-Bus and does not need (or want)
PAM to do it a second time.  This is achieved by replacing the blanket
`session include system-login` (which pulls in `pam_systemd`) with explicit
entries for just the session modules we need:

```
# atrium.arch — session modules (pam_systemd intentionally excluded)
session    required   pam_loginuid.so
session    required   pam_env.so
session    required   pam_limits.so
session    optional   pam_keyinit.so force revoke
session    required   pam_unix.so
```

This gives atrium exactly the session setup it needs (loginuid, environment,
resource limits, keyring, utmp) without the logind conflict.

### VT Keyboard Modes (`KDSKBMODE`)

When a Wayland compositor runs on a VT (virtual terminal), keyboard input
follows two separate paths simultaneously:

1. **libinput** reads raw events from `/dev/input/event*` via the evdev
   interface.  This is how the compositor (and its clients) receive keyboard
   input.

2. **The VT keyboard driver** translates raw scancodes into characters
   according to the current keyboard mode and feeds them into the TTY line
   discipline.  This is how a text console receives keyboard input.

Both paths are active at the same time.  When a Wayland compositor is
running, path 2 is a problem: characters accumulate in the TTY's input
buffer, invisible to the user.  When the compositor exits and the VT reverts
to text mode, those buffered characters are consumed by whatever runs next —
typically `agetty` / `login`.

This became a security issue in atrium when PAM authentication was wired in:
passwords typed into the GTK4 greeter's password field were simultaneously
buffered by the VT.  When the greeter's cage session ended, the password
appeared in cleartext at the getty login prompt.

The fix is to set the VT keyboard mode to `K_OFF` before launching the
compositor.  The Linux kernel supports four keyboard modes, configurable via
the `KDSKBMODE` ioctl:

| Mode           | Behavior                                          |
|----------------|---------------------------------------------------|
| `K_UNICODE`    | Translate to UTF-8 characters (text console mode) |
| `K_XLATE`      | Translate to 8-bit characters (legacy)            |
| `K_MEDIUMRAW`  | Deliver scancodes with key-up/down info           |
| `K_RAW`        | Deliver raw scancodes (used by X11)               |
| `K_OFF`        | Discard all keyboard input on this VT             |

`K_OFF` is precisely what a Wayland compositor needs: it silences path 2
completely, so no characters accumulate in the TTY buffer.  The compositor
still receives input through path 1 (libinput/evdev), which is unaffected by
the VT keyboard mode.

atrium's implementation:

```c
/* Before launching cage: */
int fd = open("/dev/ttyN", O_RDWR | O_NOCTTY);
ioctl(fd, KDGKBMODE, &saved_mode);   /* save current mode (usually K_UNICODE) */
ioctl(fd, KDSKBMODE, K_OFF);         /* silence the VT keyboard */
tcflush(fd, TCIFLUSH);               /* discard anything already buffered */

/* On shutdown: */
tcflush(fd, TCIFLUSH);               /* discard any stray input */
ioctl(fd, KDSKBMODE, saved_mode);    /* restore original mode */
close(fd);
```

The save/restore pattern is important: text consoles need `K_UNICODE` to
function, so the original mode must be restored when atrium exits (or the VT
would remain "deaf" to keyboard input).

Why `tcflush` after cage exit is *not* sufficient as the sole fix: there is a
race between the compositor exiting and the flush.  The kernel processes
buffered input asynchronously — by the time the daemon runs `tcflush`,
`agetty` may have already consumed the buffered characters.  Setting `K_OFF`
*before* the compositor starts eliminates the race entirely: no characters
ever enter the buffer.

### Custom PAM Stacks for Display Managers

Most Linux distributions provide shared PAM stack fragments — `system-auth`,
`system-login` (Arch/Fedora), `common-auth`, `common-session`
(Debian/Ubuntu) — intended for general-purpose login programs.  Using them
via `include` is convenient but can be problematic for display managers:

**`pam_systemd.so`** is included in nearly every session stack.  As described
above, it calls `CreateSession` internally, which conflicts with a display
manager that manages sessions directly.  This module must be excluded.

**`pam_selinux.so`** manages SELinux security contexts, setting the context
before session setup and optionally restoring it afterward.  On systems
without SELinux (like CachyOS), it produces harmless but noisy "unable to
get context" warnings.  On SELinux-enabled systems, it would need careful
integration with the compositor's security context.  Since atrium's target
systems don't use SELinux, these entries are omitted.

The pattern for a display manager's PAM config is to **inline the specific
modules needed** rather than including the system-wide stack:

```
# Authentication: use the system stack (safe — these are pure checks)
auth       include    system-auth

# Account: use the system stack (also pure checks)
account    include    system-login

# Session: cherry-pick individual modules (NOT system-login)
session    required   pam_loginuid.so     # audit trail
session    required   pam_env.so          # /etc/environment
session    required   pam_limits.so       # ulimits from limits.conf
session    optional   pam_keyinit.so force revoke  # session keyring
session    required   pam_unix.so         # utmp/wtmp records
# pam_systemd.so intentionally excluded
# pam_selinux.so intentionally excluded

# Password changes: use the system stack
password   include    system-auth
```

The auth, account, and password groups can safely use `include` because they
perform checks and modifications to the authentication database — not
per-process session state.  The session group needs manual curation because
it is the one that creates side effects (logind sessions, audit records,
keyrings) that can conflict with the display manager's own session
management.
