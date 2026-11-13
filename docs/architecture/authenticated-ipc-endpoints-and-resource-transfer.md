# Authenticated IPC Endpoints and Resource Transfer

## Status

The first kernel-only endpoint stage is implemented and host-tested. Public
ring-3 IPC, service discovery, blocking receive and resource attachments remain
design-only and are deliberately unavailable.

## Current boundary

The global integer-channel API has been removed. Channel lookup now starts from
the current task's endpoint table; a caller cannot use a guessed channel index
or another task's handle to reach an endpoint. Handles encode owner PID, slot
generation and slot index, but are identifiers rather than secrets or
authorization tokens.

The first implementation stage is deliberately kernel-only:

- `ipc_endpoint_pair_create()` atomically installs one endpoint in each of two
  live kernel tasks, with independent SEND/RECEIVE rights and authenticated
  peer task pointers/PIDs.
- Each endpoint handle resolves only against `scheduler_current()` and that
  task's local endpoint table.
- A channel has two independent inbound queues, one per endpoint. Each queue
  holds at most 16 messages of at most 512 bytes.
- Send is nonblocking and reports a full queue without modifying it. Receive is
  nonblocking; empty and undersized-buffer outcomes leave queue state unchanged.
  A receiver drains queued messages, then receives PEER_CLOSED when the peer
  closes, even if the receiver has no SEND right. Messages are never truncated.
- Closing one endpoint revokes its task-local handle and discards only that
  endpoint's inbound queue. Its peer remains alive until its own close, but
  sending to a closed peer fails.
- `ipc_task_close_all()` runs after a task enters EXITING and before other
  resources are torn down. Endpoint generations never wrap.
- Legacy syscall slots 91-93 are unregistered and reserved. No ring-3 IPC call
  or raw kernel pointer is exposed.

The nonblocking `ipc_recv()` remains available. Kernel services may use
`ipc_recv_wait()`, which registers the current task on its owner-bound endpoint
while interrupts are disabled, publishes TASK_WAITING_IPC/BLOCKED in the same
critical section, and sleeps with the scheduler's STI/HLT pattern. A send wakes
a waiter after publishing a complete queue entry. Endpoint close wakes local
waiters as cancelled and wakes peer waiters while preserving their inbound
queue, so queued messages drain before PEER_CLOSED. Task teardown first unlinks
the task from any peer waiter list, then revokes its own endpoints. Generic or
spurious wakeups recheck queue and peer state rather than duplicating waiter
links. This remains kernel-only; no ring-3 syscall or user pointer is exposed.

The shared-memory API remains separate and kernel-only. Its global IDs and raw
pointers are not endpoint capabilities and must not cross into ring 3.

## Required object model

1. **Channel object**: owns bounded queues, endpoint references and channel
   lifetime. It has no public integer identifier.
2. **Endpoint object**: represents one channel side and binds an owner task,
   peer task, rights, slot generation and a channel reference.
3. **Task-local endpoint handle**: a task-bound owner-PID/generation/slot
   identifier. Authorization comes from a live endpoint entry and its rights,
   not from secrecy of the integer.

Implemented endpoint rights:

- `IPC_RIGHT_SEND`: enqueue a message to the peer's inbound queue.
- `IPC_RIGHT_RECEIVE`: consume a message from this endpoint's inbound queue.

Transfer and administration rights are reserved, not implemented. Unknown,
empty or unsupported grants fail closed.

## Authenticated connection setup

A user-supplied PID or channel number is not proof of identity or permission.

A future public connection path must use a kernel-authorized rendezvous:

1. A server creates a private listening endpoint and registers a service name
   through a kernel-controlled name service.
2. Registration records server identity, service generation, listener rights
   and explicit access policy.
3. A client asks the name service to connect. The kernel derives client identity
   from the current task; it does not trust a PID supplied in the request.
4. The server receives a pending request with kernel-authenticated peer
   identity and may accept or reject it.
5. Acceptance creates a channel and two endpoint objects atomically.
6. Listener closure, task exit or service replacement invalidates pending
   requests through generation checks.

The public implementation should prefer explicit server acceptance. Do not add
wildcard public services, ambient channel lookup or global `connect(pid)`.

## Transactional resource attachment

Descriptor transfer must be an authenticated endpoint operation. A future
attachment contains only kernel-owned metadata: a pinned
`vfs_open_file_t *`, requested recipient rights, attachment type/version and
an explicit copy-versus-move policy. Copy is the only acceptable first policy.

The send path must validate endpoint identity and transfer rights, resolve
source descriptors in the sender's task, attenuate requested rights, pin each
open-file description, reserve complete queue capacity, then publish bytes and
references as one commit. Any pre-commit failure releases temporary pins and
leaves sender descriptors and queues unchanged.

The receive path must validate the full writable user destination before
reserving a message, reserve all destination descriptor slots and references,
copy through a kernel bounce buffer, and publish payload plus handles as one
transaction. Failure before commit must leave the original message available
for retry. No recipient may receive a handle with escalated rights.

## Lifecycle and concurrency invariants

- Each live endpoint owns exactly one channel reference.
- One endpoint closing must not invalidate its peer while that reference lives.
- Final channel destruction occurs only after both endpoint references are gone.
- Closing an endpoint revokes its handle before another task can reuse the slot.
- A task entering EXITING cannot receive new endpoints after its close-all pass.
- Sender identity is written by the kernel from the current task.
- No filesystem callback, user copy, allocation, scheduling operation or
  destructor may run while a channel lock is held.
- PID/generation handles are not cryptographic credentials.
- Current IRQ exclusion is single-CPU only, not an SMP synchronization design.
- Current queues are data-only. Descriptor attachments and waiter reservations
  are not implemented.

## Bounded resource policy

| Resource | Limit |
|---|---:|
| Live channel objects | 32 |
| Endpoints per task | 16 |
| Messages per direction | 16 |
| Message payload | 512 bytes |

Queue full is explicit; undersized receive buffers do not dequeue or truncate
messages. Before blocking, transfers or public ring-3 exposure, also bound
attachment counts, listener requests, outstanding reservations and waiters.

## Required regression matrix

| Scenario | Required result |
|---|---|
| Wrong task uses endpoint handle | Reject |
| Stale endpoint after close/slot reuse | Reject |
| Empty or unknown rights | Reject |
| Send without SEND right | Reject before queue mutation |
| Receive without RECEIVE right | Reject before queue mutation |
| Queue full | Explicit FULL; all existing messages intact |
| Undersized receive buffer | Explicit BUFFER_SMALL; message remains queued |
| Message larger than maximum | Reject; never truncate |
| One endpoint closes | Peer remains; queued messages drain, then receive/send report PEER_CLOSED; blocked peer readers wake |
| Message arrives while receiver waits | Message is committed before blocked receiver becomes runnable |
| Generic/spurious wake | Receiver rechecks queue and peer state without duplicate waiter links |
| Waiting task exits/is cancelled | Waiter is unlinked before its TCB can be recycled |
| Local endpoint is revoked | Blocked receive returns CANCELLED or invalid-handle, without stale endpoint access |
| Task enters EXITING | All local endpoints revoked before other teardown |
| Ring-3 IPC syscall | Reserved/unregistered and fails closed |
| Shared-memory pointer exposure | Ring-3 shared-memory calls remain denied |

A dedicated QEMU image exercises real scheduler interleavings for a blocked
reader receiving a message followed by peer exit, a peer exiting while the
reader is blocked, and local endpoint revocation cancelling a blocked reader.
The host suite continues to cover deterministic queue-link and teardown edge
cases. Future tests must cover service authorization, listener-close races,
attachment rollback, recipient table exhaustion and concurrent multi-core
operation before enabling those features.

## Implementation sequence

1. [x] Replace global integer channel lookup with task-local endpoint handles.
2. [x] Add endpoint objects, rights, generation checks and channel references.
3. [x] Add kernel-only channel-pair creation for tests and trusted services.
4. [x] Add bounded per-direction data queues with nonblocking send/receive.
5. [x] Revoke task-owned endpoints during EXITING teardown.
6. [x] Add kernel-only cancellation-safe blocking receive and endpoint waiter queues.
7. [ ] Add service-name registration and accept/reject connection lifecycle.
8. [ ] Add host tests for blocked waiter cancellation and concurrent close/send/receive.
9. [ ] Add message attachment records and queue-owned open-file references.
10. [ ] Add transactional receive-side descriptor-slot reservation and rollback.
11. [ ] Add QEMU tests for service connection, task exit and endpoint teardown.
12. [ ] Only then design ring-3 syscalls, user-memory preflight, ABI versioning
    and public service policy.

## Explicit non-goals

- No arbitrary PID-based channel access.
- No public global channel-index namespace.
- No raw kernel pointers in userspace IPC payloads.
- No ring-3 IPC or descriptor-transfer syscalls in this increment.
- No shared-memory pointer exposure until per-task mapping authority and
  revocation exist.
- No claim that the current IPC implementation is safe for multi-core use.
