# CharisOS Userspace

This directory is the starting point for the real ring-3 runtime that sits above the kernel syscall ABI.

## Intended architecture

- libc/ — C runtime and system calls
- libm/ — math helpers
- libgfx/ — framebuffer and surface helpers
- libui/ — windowing and widget toolkit
- libnet/ — socket and network helpers
- libipc/ — message-passing and shared-memory APIs
- libfs/ — filesystem and path utilities
- runtime/ — ELF startup, process bootstrap, CRT stubs
- init/ — system init and service orchestration
- shell/ — userspace command shell
- coreutils/ — standard utilities
- services/ — system daemons
- apps/ — desktop applications
- examples/ — sample programs and SDK examples

## Current status

This tree is deliberately a bootstrap scaffold rather than a completed userspace. A kernel-embedded task enters ring 3, invokes the DPL3 `int 0x80` syscall interface, and yields/resumes. `sys_exec` can replace that task with a static x86-64 `ET_EXEC`; a QEMU gate loads one from a FAT32 test disk and verifies its output.

The loader supports static ELF64 `ET_EXEC` with bounded `argv`/`envp` and an `AT_NULL`-terminated auxv. Dynamic ELF, richer auxv entries, address-space teardown, complete libc, comprehensive pointer validation, the `SYSCALL`/`SYSRET` user path, and per-task kernel interrupt stacks remain future work.

The goal of this directory is to keep the system architecture honest: userspace is tracked as an explicit subsystem, not a placeholder shell embedded in the kernel.
