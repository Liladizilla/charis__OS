# Reference-Counted Open-File Objects

## Status

Partially implemented on the current security branch. Descriptor entries now reference kernel-owned open-file descriptions, reference acquisition/release is implemented, and all in-kernel descriptor I/O consumers pin the object for their operation. Descriptor duplication, cross-task transfer, and the userspace transfer ABI remain unimplemented.

## Why the current model must change

The former `fd_entry_t` stored a raw `vfs_node_t *`, offset, flags, rights, and generation. The current entry stores an open-file pointer, rights, and generation. The open-file description owns the node, offset, and open flags. The existing node-owner registry still ensures that one closeable raw node is owned by exactly one open-file description; future duplicated descriptors must share the description rather than reclaiming the raw node.

Descriptor handles are deterministic task-scoped identifiers. They are not secrets, unforgeable capabilities, or sufficient authorization by themselves. That property must remain explicit in the ABI and documentation.

## Required object model

The target model has three distinct layers:

1. **VFS node**: filesystem/device object and its operations. It is not itself a process authority token.
2. **Open-file description**: kernel-owned object representing one successful open. It owns the node reference, shared file offset, open flags, lifecycle state, and a reference count.
3. **Descriptor entry**: task-local handle binding to an open-file description with a generation and a rights mask.

Current implementation shape (the packed lifecycle field is intentionally not a plain incrementable reference count):

```c
typedef struct vfs_open_file {
    vfs_node_t *node;
    u64 offset;
    u32 open_flags;
    volatile u32 io_lock;
    volatile u64 lifecycle;
} vfs_open_file_t;

typedef struct fd_entry {
    vfs_open_file_t *file;
    u32 rights;
    u32 generation;
} fd_entry_t;
```

The high bit of `lifecycle` is `VFS_OPEN_FILE_CLOSING`; the low 63 bits are the reference count. A compare/exchange loop increments only a nonzero, non-closing object. Final release atomically changes the one-reference value to the closing state before the destructor can run. The object layout is kernel-internal and is not a userspace ABI. `fd_acquire()` validates the current task's descriptor and rights under the descriptor-table lock, then pins the object. Every caller must pair it with exactly one `fd_release()`.

## Invariants

### Lifetime

- A newly opened node is transferred to exactly one newly created open-file description only after all allocation and validation that can fail have completed.
- Every live descriptor owns exactly one reference to its open-file description.
- A future duplicate or delegation operation must acquire one reference before publishing the recipient descriptor. No duplication or delegation API is implemented yet.
- Closing a descriptor first revokes that descriptor entry, then drops exactly one reference.
- The node close callback runs exactly once, only after the final reference is dropped.
- A reference may not be acquired from a zero-reference or closing object.
- Final destruction must not call filesystem/device callbacks while holding the descriptor-table lock or during the atomic lifecycle transition.
- A failed allocation/transfer must leave the sender's descriptor, rights, offset, and reference count unchanged.

### Authority

- Rights belong to descriptor entries, not to the shared open-file description.
- A recipient receives a subset of the sender's rights: `recipient_rights = requested_rights & sender_rights`. Reject zero rights and unknown bits.
- Process capabilities remain an independent policy gate. Descriptor rights do not replace process capability checks.
- A descriptor handle's PID/generation/slot encoding prevents accidental stale and cross-task resolution; it is not a security secret.
- Delegation must name and validate the recipient task in kernel space. A user-supplied PID alone is not proof of identity or authorization.
- No userspace pointer may be stored as an object, owner, or callback context.
- The first implementation should not add an unrestricted global descriptor lookup or an API that accepts arbitrary kernel pointers from ring 3.

### Offset and I/O semantics

- Duplicated descriptors referring to the same open-file description share one offset. This is intentional and must be tested.
- A separate `open()` creates a separate open-file description and therefore an independent offset, even when it resolves to the same node.
- Reads and writes must hold a stable object reference for the entire callback and offset update. The current syscall and ELF paths use `fd_acquire()` / `fd_release()`.
- Offset arithmetic must be checked before invoking filesystem/device callbacks.
- User buffers must continue to be completely validated/copied at the syscall boundary before callbacks with side effects.

### Concurrency and lock ordering

- Reference acquisition, descriptor publication/revocation, and the transition to final destruction must be serialized so an object cannot be resurrected after its final release.
- Do not assume atomic increment alone is sufficient: incrementing a reference count after another CPU has observed zero is invalid.
- The descriptor-table lock serializes entry publication, revocation, and the pointer-to-reference handoff. The packed atomic lifecycle protects object references and finalization. No node destructor runs under the descriptor-table lock.
- The per-open-file I/O lock serializes callbacks with shared-offset reads/updates. It disables local interrupts while held to prevent same-CPU preemption deadlocks; therefore callbacks under it must remain bounded and must not block or yield. Hosted tests use the same atomic lock without privileged interrupt instructions.
- The atomic lifecycle and table locks prevent the specific pointer/refcount races they cover; they do not by themselves establish complete SMP support for the whole kernel.

## Safe implementation sequence

1. [x] Introduce the open-file-description type and lifecycle state without exposing it to userspace.
2. [x] Migrate descriptor entries and all kernel call sites from raw node/offset fields to the open-file description.
3. [x] Add reference acquisition/release with a no-resurrection finalization rule.
4. [x] Make ordinary open create one description and make close drop one reference.
5. [x] Add host tests for operation pins surviving descriptor revocation, final callback exactly once, and independent open offsets.
6. Add a kernel-internal descriptor duplication primitive with an explicit recipient task pointer and rights attenuation.
7. Add adversarial tests for stale handles, wrong recipients, rights escalation, duplicate close, and concurrent final release.
8. Only after those tests pass, expose a constrained syscall/IPC transfer protocol to ring 3.

## Required regression matrix

| Scenario | Required result |
|---|---|
| One open, one close | One final node close |
| One descriptor plus multiple operation pins | Descriptor close keeps node alive until the final pin is released |
| Final descriptor close | Node close runs exactly once |
| Two independent opens of same path | Independent offsets and lifetimes |
| Duplicate descriptor (not implemented) | Future duplicate shares offset and adds one reference |
| Failed duplicate due to full recipient table | Sender unchanged; no leaked reference |
| Requested rights exceed sender rights | Reject or grant only the explicitly permitted intersection; never elevate |
| Unknown or zero rights | Reject |
| Stale sender handle | Reject without touching the object |
| Recipient exits | Its references are dropped; sender remains valid |
| Sender closes after delegation | Recipient remains valid while its reference exists |
| Concurrent close/final release | Exactly one finalizer; no use-after-free |
| Callback failure | Offset and object lifetime remain consistent with callback contract |

## Non-goals for this increment

- No claim of a complete object-capability system.
- No descriptor duplication or arbitrary resource sharing across address spaces yet.
- No ring-3 syscall ABI until the kernel-internal model and tests are stable.
- No cryptographic descriptor tokens.
- No broad VFS rewrite or speculative filesystem feature additions.

## Security review checklist

Before merging implementation work, verify that every descriptor consumer has migrated, every close path drops one reference, every failure path preserves ownership, every callback executes outside lifecycle locks, and the host plus QEMU suites exercise the documented invariants.
