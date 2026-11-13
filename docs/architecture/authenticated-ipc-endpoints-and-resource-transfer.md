# Authenticated IPC Endpoints and Resource Transfer

## Status

The kernel-only endpoint and service-rendezvous stages are implemented and
host-tested. QEMU verifies endpoint message wake, peer-task exit and local
endpoint cancellation. Public ring-3 IPC, public service lookup and descriptor
attachments remain deliberately unavailable.

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

Transfer rights remain reserved. Service registration is additionally gated
by CAP_IPC_SERVICE, and client capability policy is enforced by the kernel.
Unknown, empty or unsupported endpoint-right grants fail closed.

## Authenticated connection setup

A user-supplied PID or channel number is not proof of identity or permission.
The kernel-only service namespace now implements the rendezvous lifecycle:

1. A kernel task with `CAP_IPC_SERVICE` registers a unique exact service name.
   Names are bounded ASCII lowercase identifiers using letters, digits, dot,
   dash and underscore.
2. Registration may require a subset of client capability bits. The kernel
   evaluates that policy against the live client TCB, never a supplied PID or
   capability mask.
3. A client begins a connection using its current task identity and requested
   endpoint rights. A bounded generation-protected request is appended to the
   service queue; an accept waiter is woken without polling.
4. The server's blocking accept receives a request handle and kernel-derived
   client PID. It may reject the request or accept it with a subset of the
   requested client rights and independently chosen server rights.
5. Acceptance atomically creates the two owner-bound endpoint entries and
   completes the blocked client connect.
6. Unregister and task exit revoke the service, unlink accept waiters, complete
   pending clients with a closed/cancelled result and invalidate stale handles.

The service table supports 16 registrations, 16 queued requests per service
and 32 outstanding request objects globally. This remains a kernel-only API:
no syscall, user pointer, public name lookup or arbitrary PID-based connect is
exposed. Do not add wildcard public services, ambient channel lookup or global
`connect(pid)`.

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
- Current endpoint queues are data-only. Blocking message waiters and service
  connect/accept waiters are implemented; descriptor-attachment reservations
  and transactional transfer are not.

## Bounded resource policy

| Resource | Limit |
|---|---:|
| Live channel objects | 32 |
| Endpoints per task | 16 |
| Named services | 16 |
| Outstanding connection requests | 32 |
| Pending requests per service | 16 |
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
| Duplicate or malformed service name | Reject |
| Client lacks service-required capabilities | ACCESS_DENIED before request publication |
| Service request grants more than client requested | Reject |
| Service unregister/task exit | Wake acceptors and clients; stale service/request handles fail |
| Ring-3 IPC syscall | Reserved/unregistered and fails closed |
| Shared-memory pointer exposure | Ring-3 shared-memory calls remain denied |

Host tests cover waiter unlinking on task teardown. The dedicated QEMU test
proves the receiver is actually BLOCKED before send, wakes after message commit,
blocks again and wakes when its peer task exits through the real task-exit
cleanup path. A second pair verifies local endpoint revocation while the reader
is BLOCKED: the reader returns CANCELLED, the peer's still-open handle returns
PEER_CLOSED, and the coordinator reaps both workers with waitpid. The verifier
requires the message-wake, peer-close, cancellation and final-success markers.
Further adversarial stress must cover service authorization, listener-close
races, attachment rollback, recipient table exhaustion and concurrent multi-core
operation before enabling those features.

## Implementation sequence

1. [x] Replace global integer channel lookup with task-local endpoint handles.
2. [x] Add endpoint objects, rights, generation checks and channel references.
3. [x] Add kernel-only channel-pair creation for tests and trusted services.
4. [x] Add bounded per-direction data queues with nonblocking send/receive.
5. [x] Revoke task-owned endpoints during EXITING teardown.
6. [x] Add kernel-only cancellation-safe blocking receive and endpoint waiter queues.
7. [x] Add kernel-only named service registration, client-capability policy and blocking connect/accept/reject lifecycle.
8. [x] Add host service tests for registration policy, request rights, rejection, unregister/client exit, table limits and generation exhaustion.
9. [ ] Add QEMU tests for blocked client connect/server accept wakeups and service teardown races.
10. [ ] Add message attachment records and queue-owned open-file references.
11. [ ] Add transactional receive-side descriptor-slot reservation and rollback.
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
