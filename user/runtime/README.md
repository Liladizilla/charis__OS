# Userspace runtime

The runtime is responsible for ELF startup, stack setup, initial process state, and the transition from a kernel-created task into a userspace execution context.

Planned responsibilities:

- load ELF binaries
- allocate a user stack
- initialize argc/argv/envp
- set thread-local or process-local state
- dispatch to the entry point in ring 3
- handle process exit and cleanup

The runtime must not rely on kernel-only function addresses from ring 3; once the task reaches userspace, all behavior must use the syscall ABI and the user page tables.
