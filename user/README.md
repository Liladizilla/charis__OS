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

This tree is deliberately a bootstrap scaffold rather than a completed userspace. A kernel-embedded test task now enters ring 3, invokes the DPL3 `int 0x80` syscall interface, yields to a kernel task, and resumes. The boot gate verifies that round trip in QEMU.

This does not yet load independent ELF programs. The ELF-to-task handoff, complete libc, pointer validation, `SYSCALL`/`SYSRET` user path, and per-task kernel interrupt stacks remain future work.

The goal of this directory is to keep the system architecture honest: userspace is tracked as an explicit subsystem, not a placeholder shell embedded in the kernel.
