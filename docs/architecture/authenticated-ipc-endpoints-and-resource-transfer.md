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

## Kernel service namespace and authenticated rendezvous

The first kernel-only service namespace is implemented:

- The global table is bounded to 16 service records. Names are unique,
  case-sensitive ASCII identifiers of 1-31 characters from
  `[A-Za-z0-9._-]`.
- Registration is bound to the current live kernel task. A service must provide
  a nonzero `required_client_capabilities` mask containing only bits in
  `CAP_ALL`; unknown or zero masks fail closed. Client authorization is
  delegated to the existing `security_check_capability()` policy, which
  requires every requested bit to be present in the current task's
  kernel-assigned `capabilities` field.
- Registration assigns server and client endpoint rights. A client cannot
  request stronger rights or supply a trusted PID.
- Service handles encode the owner PID, slot generation and slot. Resolution
  requires the current server task to match the stored owner pointer and PID.
  Generations never wrap; exhausted slots are permanently retired.
- The pending request table is bounded to 32 records. Request handles are
  scoped to the connecting task's PID and a separate generation/slot pair.
  The kernel derives client identity from `scheduler_current()`.
- `ipc_service_next()` claims the oldest pending request for the registering
  service and returns its opaque request token plus the kernel-observed client
  PID. A different service cannot claim or accept that token.
- The owner may call `ipc_service_accept()` or `ipc_service_reject()`.
  Acceptance atomically installs both endpoint objects using rights selected
  at registration. The server receives its endpoint immediately; the client
  obtains its endpoint once through `ipc_service_result()`.
- Connect and result retrieval are nonblocking. A client polls its own request
  token and receives EMPTY until the server accepts or rejects.
- `ipc_service_cancel()` is scoped to the current client task. It invalidates
  pending/offered tokens so a server's saved offer can no longer be accepted.
  If acceptance already committed but the client has not retrieved its handle,
  cancellation closes that endpoint and wakes the server with PEER_CLOSED.
- Unregister and server task exit reject unaccepted/offered requests. Accepted
  endpoints are independent channel references and survive service unregister
  until their owning tasks close them. Client task exit discards its request
  records and closes accepted endpoints during normal task cleanup.
- Dead clients discovered while the server scans pending requests are reclaimed
  rather than consuming request-table slots indefinitely.
- Duplicate names, invalid name characters, zero capability policy, unknown
  service handles, cross-task request tokens and exhausted tables fail closed.
- `ipc_init()` is one-shot so reinitialization cannot reset generations and
  revive stale service/request tokens.

This is still a kernel-only API. It does not register user services, expose
names or request handles to ring 3, or create a public syscall. It also does
not yet provide blocking connect, request timeout/requeue, listener wildcard
matching or SMP synchronization. The current IRQ exclusion is valid only for
the single-CPU scheduler. Do not add global `connect(pid)`, client-selected
endpoint rights or ambient integer channel lookup.

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
| Services in kernel namespace | 16 |
| Pending connection requests | 32 |
| Endpoints per task | 16 |
| Messages per direction | 16 |
| Message payload | 512 bytes |
| Service name length | 1-31 ASCII characters |

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
| Client lacks required service capabilities | Deny through security_check_capability() before request allocation |
| Service requests undefined capability bits | Reject registration against CAP_ALL |
| Zero-capability public service registration | Reject |
| Duplicate service name | Reject without changing existing service |
| Wrong task uses service handle | Reject |
| Wrong service attempts to accept request token | Reject |
| Server rejects request | Client receives REJECTED once; no endpoint is installed |
| Client cancels pending/offered request | Request token is revoked; stale server offer cannot be accepted |
| Client cancels accepted, unclaimed request | Client endpoint is closed and server observes PEER_CLOSED |
| Service unregisters with pending/offered requests | Reject pending work; accepted channel pairs remain independent |
| Client exits with pending request | Release request record before TCB reuse |
| Dead client discovered by server scan | Reclaim its request record |
| Server exits | Revoke registrations and reject unaccepted requests |
| Reused service/request slot | Old generation token remains invalid |

Host tests cover waiter unlinking on task teardown and service
authorization/request lifecycle. A dedicated QEMU test proves the receiver is
actually BLOCKED before send, wakes after message commit, blocks again and wakes
on peer close through the real scheduler. Remaining adversarial work includes
service-rendezvous QEMU coverage, concurrent close/send/receive and cancellation
stress, listener-close races, attachment rollback, recipient table exhaustion
and multi-core synchronization before public exposure.

## Implementation sequence

1. [x] Replace global integer channel lookup with task-local endpoint handles.
2. [x] Add endpoint objects, rights, generation checks and channel references.
3. [x] Add kernel-only channel-pair creation for tests and trusted services.
4. [x] Add bounded per-direction data queues with nonblocking send/receive.
5. [x] Revoke task-owned endpoints during EXITING teardown.
6. [x] Add kernel-only cancellation-safe blocking receive and endpoint waiter queues.
7. [x] Add a kernel-only bounded service namespace with explicit capability policy and accept/reject rendezvous.
8. [x] Add host tests for service authorization, cross-task tokens, request lifecycle, task exit and table limits.
9. [x] Add dedicated QEMU waiter coverage for actual BLOCKED state, message wake, repeated wait and peer-close wake.
10. [ ] Add QEMU service-rendezvous coverage plus concurrent close/send/receive and waiter cancellation stress.
11. [ ] Add message attachment records and queue-owned open-file references.
12. [ ] Add transactional receive-side descriptor-slot reservation and rollback.
13. [ ] Only then design ring-3 syscalls, user-memory preflight, ABI versioning
     and public service policy.

## Explicit non-goals

- No arbitrary PID-based channel access.
- No public global channel-index namespace.
- No raw kernel pointers in userspace IPC payloads.
- No ring-3 IPC or descriptor-transfer syscalls in this increment.
- No shared-memory pointer exposure until per-task mapping authority and
  revocation exist.
- No claim that the current IPC implementation is safe for multi-core use.
