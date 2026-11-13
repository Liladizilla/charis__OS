# Reference-Counted Open-File Objects

## Status

Implemented kernel-internal lifetime, duplication and two-phase transfer primitives on the integration branch. Descriptor entries reference kernel-owned open-file descriptions; syscall and ELF I/O pin them; `fd_duplicate_to()` attenuates rights; and `fd_transfer_reserve()` / `fd_transfer_commit()` / `fd_transfer_cancel()` reserve recipient capacity, publish an already-pinned reference or cancel without reusing the generation. IPC uses these primitives for kernel-only message attachments. The userspace transfer ABI remains intentionally unimplemented.

## Why the current model must change

The former `fd_entry_t` stored a raw `vfs_node_t *`, offset, flags, rights, and generation. The current entry stores an open-file pointer, rights, an unpublished-slot reservation flag and generation. The open-file description owns the node, offset, and open flags. The existing node-owner registry still ensures that one closeable raw node is owned by exactly one open-file description. `fd_duplicate_to()` shares that description rather than reclaiming or reattaching the raw node.

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
    bool reserved;
    u32 generation;
} fd_entry_t;

typedef struct fd_transfer_reservation {
    u32 recipient_pid;
    u32 slot;
    u32 generation;
    u32 rights;
} fd_transfer_reservation_t;
```

The high bit of `lifecycle` is `VFS_OPEN_FILE_CLOSING`; the low 63 bits are the reference count. A compare/exchange loop increments only a nonzero, non-closing object. Final release atomically changes the one-reference value to the closing state before the destructor can run. The object layout is kernel-internal and is not a userspace ABI. `fd_acquire()` validates the current task's descriptor and rights under the descriptor-table lock, then pins the object. Every caller must pair it with exactly one `fd_release()`.

## Invariants

### Lifetime

- A newly opened node is transferred to exactly one newly created open-file description only after all allocation and validation that can fail have completed.
- Every live descriptor owns exactly one reference to its open-file description.
- A reserved descriptor slot owns no file reference and cannot be resolved, opened over or duplicated into.
- `fd_duplicate_to()` acquires one reference before publishing the recipient descriptor. It rejects recipients in UNUSED, ALLOCATING, EXITING or ZOMBIE state and rejects a requested rights mask that is not a nonzero subset of the sender's grant.
- `fd_transfer_reserve()` marks one empty recipient slot reserved and burns its next generation without acquiring a reference or exposing a handle. `fd_transfer_commit()` transfers one caller-owned pin into that exact generation; failure leaves pin ownership with the caller. `fd_transfer_cancel()` clears only a matching unpublished reservation, and the burned generation is never reused.
- Closing a descriptor first revokes that descriptor entry, then drops exactly one reference.
- The node close callback runs exactly once, only after the final reference is dropped.
- A reference may not be acquired from a zero-reference or closing object.
- Final destruction must not call filesystem/device callbacks while holding the descriptor-table lock or during the atomic lifecycle transition.
- A failed allocation/transfer must leave the sender's descriptor, rights, offset, and reference count unchanged.

### Authority

- Rights belong to descriptor entries, not to the shared open-file description.
- A recipient receives only the exact requested subset of the sender's rights. `fd_duplicate_to()` rejects zero rights, unknown bits, and any requested bit not present in the source descriptor. Rights remain independent on the two descriptor entries.
- Process capabilities remain an independent policy gate. Descriptor rights do not replace process capability checks.
- A descriptor handle's PID/generation/slot encoding prevents accidental stale and cross-task resolution; it is not a security secret.
- The kernel-internal `fd_duplicate_to()` names the recipient by PID and resolves it through the kernel task table; it never accepts a userspace task pointer. This primitive is not exposed to ring 3, and a PID alone is not treated as a public authorization credential.
- No userspace pointer may be stored as an object, owner, or callback context.
- The first implementation should not add an unrestricted global descriptor lookup or an API that accepts arbitrary kernel pointers from ring 3.

### Offset and I/O semantics

- Descriptors created by `fd_duplicate_to()` refer to the same open-file description and share one offset. Host regression tests verify the same object and shared offset.
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
6. [x] Add a kernel-internal descriptor duplication primitive that resolves an explicit recipient PID through the kernel task table and attenuates rights.
7. [x] Add host tests for rights escalation rejection, recipient capacity exhaustion, sender close, recipient cleanup, shared offset, and rejected exiting/zombie recipients.
8. [x] Add a kernel-only two-phase descriptor reserve/commit/cancel API for trusted IPC attachment delivery; reserved slots are unavailable to ordinary opens and duplicates.
9. [x] Add host regression coverage for reservation cancellation, stale reservation tokens, table exhaustion, recipient exit and reference ownership.
10. Keep any syscall/IPC transfer protocol out of ring 3 until user-memory preflight and a versioned public ABI are separately designed and tested.

## Required regression matrix

| Scenario | Required result |
|---|---|
| One open, one close | One final node close |
| One descriptor plus multiple operation pins | Descriptor close keeps node alive until the final pin is released |
| Final descriptor close | Node close runs exactly once |
| Two independent opens of same path | Independent offsets and lifetimes |
| Kernel-internal duplicate | Shares offset, adds one reference, preserves descriptor-local rights |
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
- No userspace descriptor-transfer syscall or general resource-sharing protocol yet.
- No ring-3 syscall ABI until the kernel-internal model and tests are stable.
- No cryptographic descriptor tokens.
- No broad VFS rewrite or speculative filesystem feature additions.

## Security review checklist

Before merging implementation work, verify that every descriptor consumer has migrated, every close path drops one reference, every failure path preserves ownership, every callback executes outside lifecycle locks, and the host plus QEMU suites exercise the documented invariants.
