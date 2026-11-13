# Authenticated IPC Endpoints and Resource Transfer

## Status

The kernel-only endpoint, service-rendezvous and one-file descriptor-attachment
stages are implemented and host-tested. QEMU verifies endpoint message wake,
peer-task exit, local endpoint cancellation, blocked service accept/connect
wakeup, rights-limited endpoint messaging, request rejection and unregister
waking a blocked client. Attachment tests verify queue-owned open-file references,
rights attenuation, receiver-slot exhaustion, commit rollback, queue preservation
and teardown release. Public ring-3 IPC and public service lookup remain
deliberately unavailable.

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

The first kernel-only attachment increment is implemented. Each IPC message
may carry at most one `vfs_open_file_t *` reference plus the requested
recipient-rights mask. The pointer is kernel-owned metadata and is never
serialized into a userspace payload. No attachment type/version field exists
yet because this protocol admits only one open-file description.

The sender calls `ipc_send_with_fd()`. It resolves and pins the source
descriptor in the current task before entering the IPC critical section. The
sender endpoint must have both `IPC_RIGHT_SEND` and `IPC_RIGHT_TRANSFER`; the
requested descriptor rights must already be granted by the source entry. The
queue takes ownership of the temporary reference only when the payload and
attachment are committed together. Queue full, peer-close, invalid endpoint or
source-right failure leaves the sender descriptor unchanged and drops the
temporary pin outside the IPC lock.

A normal `ipc_recv()` does not silently discard an attached reference. It
returns `IPC_STATUS_ATTACHMENT_PENDING` and leaves that message at the queue
head. `ipc_recv_with_fd()` is required to accept it. The receiving endpoint
must have `IPC_RIGHT_RECEIVE` and `IPC_RIGHT_TRANSFER`. The receiver reserves
a task-local descriptor slot using `fd_transfer_reserve()`, then publishes
the queue-owned reference with `fd_transfer_commit()`. Only after a successful
commit does it copy the bounded payload and dequeue the message. The recipient
gets exactly the rights carried by the attachment, which were checked against
the source descriptor. If the destination table is full or commit fails, the
reservation is cancelled and the original message and its reference remain
queued for retry.

Closing an endpoint or tearing down a task detaches queued attachment
references while local IRQ exclusion is held, then calls `fd_release()` only
after the IPC critical section has been restored. The release batch is bounded
by the per-task endpoint and per-direction queue limits. No filesystem close
callback or heap destructor runs while the IPC lock is held.

This increment is deliberately kernel-only. The receive buffer is a kernel
buffer, and these APIs are not registered as syscalls. It does not yet provide
user-memory preflight, a ring-3 ABI, arbitrary multi-file bundles, move
semantics, shared-memory transfer, attachment metadata/version negotiation or
SMP-safe synchronization. Public IPC must remain unavailable until those
separate authority and user-copy requirements are implemented and tested.

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
- Endpoint queues support one kernel-only open-file attachment per message.
  Queue-owned references move into reserved recipient descriptor slots on
  receive and are released outside the IPC critical section on teardown.
  No ring-3 pointer or syscall is exposed.

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
| Attachment sent without SEND or TRANSFER right | Reject before queue mutation |
| Attachment requests rights absent from source descriptor | Reject; source unchanged |
| Normal receive encounters an attachment | ATTACHMENT_PENDING; preserve queue head |
| Receiver descriptor table is full | DESCRIPTOR_TABLE_FULL; preserve message and file reference |
| Descriptor commit fails | Cancel slot reservation; preserve message for retry |
| Receiver accepts attachment | Publish only the explicitly requested rights subset |
| Queued attachment is abandoned on endpoint/task teardown | Release every queue-owned reference exactly once, after IPC unlock |
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

Host tests cover waiter unlinking on task teardown and the attachment transaction:
sender close after enqueue, data-only receive preservation, receiver slot
exhaustion, commit failure and retry, transfer-right denial, queue-close release,
task-exit release and multi-message cleanup. They assert that descriptor pin
acquisition and final release run outside the IPC critical section, while
reservation and commit run inside it. The dedicated QEMU test
proves the receiver is actually BLOCKED before send, wakes after message commit,
blocks again and wakes when its peer task exits through the real task-exit
cleanup path. A second pair verifies local endpoint revocation while the reader
is BLOCKED: the reader returns CANCELLED, the peer's still-open handle returns
PEER_CLOSED, and the coordinator reaps both workers with waitpid. The verifier
requires the message-wake, peer-close, cancellation and final-success markers.
Dedicated QEMU coverage now verifies a real server blocked in accept, a
real client blocked in connect, successful wake and endpoint delivery, the
granted SEND/RECEIVE subset, explicit rejection, and service unregister while a
client is demonstrably BLOCKED. The CI verifier requires all corresponding
serial markers. Further adversarial stress must cover service-owner exit during
blocked accept, concurrent clients/acceptors, cancellation and timeout policy,
attachment rollback, recipient table exhaustion and concurrent multi-core
operation before exposing a public ABI.

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
10. [x] Add one-file kernel-only message attachments with queue-owned open-file references.
11. [x] Add transactional receive-side descriptor-slot reservation, rights attenuation, retry-safe commit rollback and teardown release.
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
