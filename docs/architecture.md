# atrium - Architecture

## Starting a session

TODO: we only concern ourselves with non-seat0 seats for now. seat0 is special due to its associated VT.

When starting a graphical session, two things must happen:

- Call the D-Bus `CreateSession` IPC: logind will assign the seat's devices (DRM and input) to that seat explicitly, according to the udev rules, and grant the user access to those devices.
- Fork a child process and `exec` the compositor (within a login session).

The execution order here is tricky: we must pass the child process's PID to CreateSession, so logind can track it as the session leader. However, the child must not `exec` the compositor until `CreateSession` is called and has succeeded in setting up the session.

Hence, after the fork, the child must wait until the parent signals that the session is ready. This can be achieved via a synchronization pipe: the parent creates the pipe and passes it to the child. The child tries to read from it, which will block. Once the session is ready, the parent closes the pipe, which unblocks the child. At this point everything is ready for the child to proceed launching the compositor.  
