# Process lifecycle and wait/reap contract

## Scope

This document describes the implemented CharisOS parent/child lifecycle and
`SYS_WAITPID`. It does not claim that `fork()`, process groups, sessions,
or an untrusted user process-spawn API exist.

## Identity and ownership

Every constructed task receives a monotonic, non-reused PID. The task records
the PID of the task that created it in `parent_pid`. A wait operation can
collect only a direct child of the calling task. A caller cannot inspect or
reap an unrelated task by guessing its PID.

The supported selectors are:

- A positive PID: wait for that exact direct child.
- `-1`: wait for any direct child.

Zero, values below `-1`, non-child PIDs, and unknown PIDs fail. Process-group
selectors are not implemented.

## Syscall contract

`SYS_WAITPID` uses:

| Register argument | Meaning |
|---|---|
| `a1` | Child PID, or `-1` for any child |
| `a2` | Optional pointer to a 32-bit exit status |
| `a3` | Options; only `WAITPID_WNOHANG` is supported |

Return values are the reaped child PID, zero when `WNOHANG` finds a matching
live child, or `-1` for an invalid selector, unsupported option, missing
child, or internal lifecycle invariant failure.

For ring 3, the kernel validates the entire writable status destination before
blocking or consuming a zombie. The final copy-out repeats range validation.
A null status pointer discards the exit status.

## State and lifetime ordering

1. Publish `TASK_STATE_EXITING` with local interrupts disabled.
2. Reparent descendants and reclaim detached zombie children.
3. Close descriptors and destroy private user mappings after switching CR3.
4. Remove the task from the scheduler and wake a matching parent.
5. Switch away and release the exiting task's guarded kernel stack.
6. Publish `TASK_STATE_ZOMBIE` only after the stack release is complete.
7. Allow the parent to collect status and recycle the task-table slot.

A slot cannot become reusable while the CPU is still executing on its kernel
stack. The woken parent resumes only after the scheduler's post-switch release
path has completed.

## Orphans and slot reuse

When a parent exits, live descendants are reparented to PID zero. Detached
zombies with no parent are reclaimed immediately; live orphans are reclaimed
after their own stacks have been detached. Descriptor generations and the
stable task slot index survive reuse; PIDs are never recycled.

## Deliberate limitations

- `fork()`, copy-on-write, process groups and sessions are not implemented.
- There is no user-controlled spawn syscall yet; kernel task constructors
  establish parent-child relationships.
- Only `WNOHANG` is accepted as an option.
- The scheduler and task table remain single-CPU structures. Local interrupt
  exclusion is not an SMP synchronization design.
- The QEMU lifecycle probe uses a kernel parent because ring-3 process creation
  is not yet exposed. It tests the real wait syscall handler and scheduling path.

## Regression coverage

The host policy test checks selector validity, direct-child ownership and
nonblocking result rules. A dedicated QEMU image verifies exact-child wait,
any-child wait, exit status, `WNOHANG`, invalid/non-child selectors, slot
reclamation, and orphan auto-reaping.
