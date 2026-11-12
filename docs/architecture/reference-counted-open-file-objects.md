# Reference-Counted Open-File Objects

## Status

Architecture contract for the next VFS security increment. This document specifies the intended model; it does not claim that reference-counted sharing or descriptor delegation is implemented yet.

## Why the current model must change

The current `fd_entry_t` stores a raw `vfs_node_t *`, an offset, flags, rights, and a generation. A closeable node is globally claimed by one descriptor. This prevents duplicate ownership and double-close, but it also prevents safe sharing between tasks. Removing that restriction without introducing a separate lifetime object would create use-after-free and double-destruction risks.

Descriptor handles are deterministic task-scoped identifiers. They are not secrets, unforgeable capabilities, or sufficient authorization by themselves. That property must remain explicit in the ABI and documentation.

## Required object model

The target model has three distinct layers:

1. **VFS node**: filesystem/device object and its operations. It is not itself a process authority token.
2. **Open-file description**: kernel-owned object representing one successful open. It owns the node reference, shared file offset, open flags, lifecycle state, and a reference count.
3. **Descriptor entry**: task-local handle binding to an open-file description with a generation and a rights mask.

Conceptual layout (names are illustrative until the implementation patch lands):

```c
typedef struct vfs_open_file {
    vfs_node_t *node;
    u64 offset;
    u32 open_flags;
    u32 references;
    u32 state;
} vfs_open_file_t;

typedef struct fd_entry {
    vfs_open_file_t *file;
    u32 rights;
    u32 generation;
} fd_entry_t;
```

The implementation must use the repository's actual integer types and synchronization primitives. Do not copy this sketch as an unreviewed ABI.

## Invariants

### Lifetime

- A newly opened node is transferred to exactly one newly created open-file description only after all allocation and validation that can fail have completed.
- Every live descriptor owns exactly one reference to its open-file description.
- A successful duplicate or delegation operation acquires one reference before publishing the recipient descriptor.
- Closing a descriptor first revokes that descriptor entry, then drops exactly one reference.
- The node close callback runs exactly once, only after the final reference is dropped.
- A reference may not be acquired from a zero-reference or closing object.
- Final destruction must not call filesystem/device callbacks while holding the global object-table or reference-management lock.
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
- Reads and writes must hold a stable object reference for the entire callback and offset update.
- Offset arithmetic must be checked before invoking filesystem/device callbacks.
- User buffers must continue to be completely validated/copied at the syscall boundary before callbacks with side effects.

### Concurrency and lock ordering

- Reference acquisition, descriptor publication/revocation, and the transition to final destruction must be serialized so an object cannot be resurrected after its final release.
- Do not assume atomic increment alone is sufficient: incrementing a reference count after another CPU has observed zero is invalid.
- Establish and document one lock order before enabling concurrent transfer. Never call node callbacks, allocate memory, or free objects while holding a spinlock unless the callback contract explicitly guarantees it is safe.
- If the kernel is currently uniprocessor-only, document that constraint and still make state transitions explicit. Do not claim SMP safety based only on atomic integer operations.

## Safe implementation sequence

1. Introduce the open-file-description type and lifecycle state without exposing it to userspace.
2. Migrate descriptor entries and all kernel call sites from `entry->node/offset` to `entry->file->node/offset`.
3. Add reference acquisition/release with a no-resurrection finalization rule.
4. Make ordinary open create one description and make close drop one reference.
5. Add host tests for allocation failure, duplicate references, close ordering, final callback exactly once, and independent opens.
6. Add a kernel-internal descriptor duplication primitive with an explicit recipient task pointer and rights attenuation.
7. Add adversarial tests for stale handles, wrong recipients, rights escalation, duplicate close, and concurrent final release.
8. Only after those tests pass, expose a constrained syscall/IPC transfer protocol to ring 3.

## Required regression matrix

| Scenario | Required result |
|---|---|
| One open, one close | One final node close |
| One open, two descriptor references | First close keeps node alive |
| Final descriptor close | Node close runs exactly once |
| Two independent opens of same path | Independent offsets and lifetimes |
| Duplicate descriptor | Shared offset and one additional reference |
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
- No arbitrary resource sharing across address spaces.
- No ring-3 syscall ABI until the kernel-internal model and tests are stable.
- No cryptographic descriptor tokens.
- No broad VFS rewrite or speculative filesystem feature additions.

## Security review checklist

Before merging implementation work, verify that every descriptor consumer has migrated, every close path drops one reference, every failure path preserves ownership, every callback executes outside lifecycle locks, and the host plus QEMU suites exercise the documented invariants.
