# libc

The libc layer will provide a minimal runtime for CharisOS applications:

- memory allocation
- string handling
- path and directory utilities
- file descriptors and stdio
- process and signal wrappers
- environment variables
- basic math helpers
- formatted output

The kernel syscall ABI is not Linux-compatible, so the C runtime must provide CharisOS-specific wrapper functions rather than hoping the kernel exposes a generic POSIX ABI.
