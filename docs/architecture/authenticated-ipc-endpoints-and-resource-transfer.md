# Authenticated IPC Endpoints and Resource Transfer

## Status

Design contract only. The current IPC subsystem is not suitable for ring-3 use or descriptor transfer. Its syscalls remain kernel-only. This document defines the security and lifetime requirements for replacing the global integer-channel API; it does not claim those endpoint semantics are implemented.

## Current boundary

The current implementation has a global fixed array of channels selected by small integer indexes. `ipc_create()` does not bind an owner or enforce a namespace, `ipc_send()` and `ipc_recv()` accept the raw channel index, and the payload is copied through a kernel pointer. Channel generations help a blocked receiver detect slot reuse, but they do not authenticate a sender, receiver, or endpoint holder.

The syscall handlers deny all ring-3 IPC calls. This restriction is intentional. The existing shared-memory API also returns a raw kernel pointer and uses globally scoped integer IDs; its user-facing handlers must remain denied until shared-memory objects have per-task ownership, mapping permissions, and teardown rules.

Do not expose the current integer channel IDs or shared-memory IDs as a process security boundary.

## Required object model

Use three kernel-owned layers:

1. **Channel object**: owns the bounded message queue, channel state, endpoint references, wait queues, and a monotonic generation/identity. It is not directly addressable by a userspace integer.
2. **Endpoint object**: represents one side of a channel. It binds to a kernel-resolved task identity, peer identity, allowed operations, and a channel reference. A channel pair has distinct endpoint objects for each side.
3. **Task-local endpoint handle**: a generation-protected task-bound identifier referring to an endpoint object. It is an identifier, not a secret or authorization credential.

The endpoint's rights are independent and explicit:

- `IPC_RIGHT_SEND`: enqueue a message.
- `IPC_RIGHT_RECEIVE`: receive a message.
- `IPC_RIGHT_TRANSFER`: attach a permitted kernel resource to a message.
- `IPC_RIGHT_ADMIN`: close or reconfigure an endpoint only where the endpoint creation policy grants this authority.

Unknown, empty, or unsupported rights fail closed. Endpoint duplication must use rights attenuation only. A sender cannot grant a right it does not hold.

## Authenticated connection setup

A user-supplied PID or channel number is not proof of identity or permission.

A future connection path must use a kernel-authorized rendezvous:

1. A server creates a private listening endpoint and registers a service name through a kernel-controlled name service.
2. Registration records the server task identity, a service generation, the listener's rights, and an explicit access policy.
3. A client asks the name service to connect. The kernel derives the client identity from the current task; it does not trust a PID supplied in the request.
4. The server receives a pending connection request containing kernel-authenticated peer identity and may accept or reject it.
5. Acceptance creates a channel object and two endpoint objects atomically. The client receives only its endpoint; the server receives only its endpoint. Each endpoint stores the peer identity established by the kernel.
6. Listener closure, task exit, or service replacement invalidates pending requests through generation checks. A stale service name or endpoint must never resolve to a new owner.

The initial implementation should prefer explicit server acceptance over automatic connection. Do not add wildcard public services, ambient channel lookup, or a global `connect(pid)` interface.

## Transactional resource attachment

Descriptor transfer is an operation on an authenticated endpoint, not a way to submit a descriptor integer to an arbitrary process.

A future message attachment contains only kernel-owned metadata:

- a pinned `vfs_open_file_t *` reference (never a userspace pointer);
- the requested recipient rights;
- an attachment type/version;
- an explicit copy-versus-move policy, with copy as the only initial policy.

The send path must:

1. Verify the sender's endpoint handle, task identity, and `IPC_RIGHT_SEND | IPC_RIGHT_TRANSFER`.
2. Resolve each source descriptor in the current task and verify that requested recipient rights are a nonzero subset of its descriptor rights.
3. Pin each open-file description before queue publication.
4. Reserve a complete queue slot and attachment capacity before modifying sender state.
5. Publish message bytes and all attachment references as one commit.
6. On any pre-commit failure, release every temporary pin and leave the sender's descriptors and channel queue unchanged.

The receive path must:

1. Verify the receiver's endpoint handle, task identity, and `IPC_RIGHT_RECEIVE`.
2. Validate the complete writable userspace output span before reserving or consuming a message. Never dequeue first and discover an invalid user pointer afterward.
3. Reserve one queued message against concurrent receivers without holding a spinlock across user copyout or callbacks.
4. Reserve all recipient descriptor slots and acquire the corresponding open-file references before publication.
5. Copy the message into a kernel bounce buffer and commit the endpoint handles and payload as one transaction.
6. If allocation, validation, or copyout fails before commit, remove the reservation and leave the original message and its attachment references available to retry.
7. After successful commit, remove the queued message and release its queue-owned references. The recipient descriptors now own their references.

No step may publish a partially attached message. A recipient must never receive a handle whose rights exceed the sender's granted rights or the endpoint transfer policy.

## Lifecycle and concurrency invariants

- Each live endpoint owns exactly one channel reference.
- Each queued attachment owns exactly one open-file reference.
- A delivered descriptor owns one reference before the queue relinquishes its reference.
- Closing one endpoint must not invalidate the peer endpoint while the peer still owns a reference.
- Final channel destruction occurs only after both endpoint and in-flight operation references have been released.
- Closing a channel wakes all waiters and makes them re-check both endpoint generation and channel state.
- A task entering EXITING cannot receive new endpoint handles after its endpoint-table close pass.
- Endpoint-table publication, revocation and operation pinning must share a documented lock order.
- No filesystem callback, user copy, memory allocation, task scheduler operation, or destructor may run while holding a channel spinlock.
- Message queue reservations must have a cancellation path for receiver task exit and endpoint closure.
- Sender identity in message metadata is written by the kernel from the current task, never copied from a user-supplied field.
- PID/generation handles are not cryptographic credentials. Authorization comes from possession of a live task-local endpoint entry plus its rights and the kernel-established peer policy.

## Bounded resource policy

The first implementation must set hard limits for:

- queued message bytes;
- messages per endpoint;
- attachment count per message;
- outstanding connection requests per listener;
- open endpoints per task;
- total live channel objects;
- blocked receivers per endpoint.

Overflow must fail without corrupting queue indices, leaking references, or silently truncating a resource attachment. A truncated data-only message may be allowed only if the API explicitly reports the delivered length; attached resources are never silently dropped.

## Required regression matrix

| Scenario | Required result |
|---|---|
| Client guesses a channel integer | No authority is gained |
| Client supplies another task's PID | Request identity is derived from current task; supplied PID is ignored/rejected |
| Unauthorized service lookup | Connection rejected without disclosing endpoint internals |
| Server rejects pending request | No client endpoint or channel reference leaks |
| Listener closes during connection | Pending request invalidated by generation |
| Wrong task uses endpoint handle | Reject |
| Stale endpoint after slot reuse | Reject |
| Send without SEND right | Reject before queue mutation |
| Transfer without TRANSFER right | Reject before resource pinning |
| Requested descriptor rights exceed source rights | Reject with sender unchanged |
| Queue full | Sender descriptors unchanged; no leaked pins |
| Receive into invalid or read-only span | Message remains queued |
| Recipient descriptor table full | Message remains queued |
| Receiver exits during reservation | Reservation cancelled; message remains deliverable |
| Endpoint closes with blocked receivers | Every waiter wakes and returns a defined error |
| One endpoint closes | Peer remains valid until its own final release |
| Queued attachment discarded on channel teardown | Its file reference is released exactly once |
| Concurrent send/receive/close | No double dequeue, queue corruption, or reference leak |
| Attachment copy succeeds | Recipient gets a task-bound handle with attenuated rights |
| Sender exits after copy delivery | Recipient reference remains valid |
| Sender exits before delivery | Queue cleanup or delivery policy releases/retains the pinned reference exactly as specified |

## Implementation sequence

1. [ ] Replace global integer channel lookup with task-local endpoint handles.
2. [ ] Add endpoint objects, endpoint rights, generation checks, and channel references.
3. [ ] Add kernel-only channel-pair creation for tests and trusted services.
4. [ ] Add explicit listener registration and accept/reject lifecycle with a kernel-owned service namespace.
5. [ ] Add cancellation-safe message reservation and per-endpoint bounded queues.
6. [ ] Add host tests for ownership, wrong-task use, stale handles, close/waiter races, and queue limits.
7. [ ] Add message attachment records and queue-owned open-file references.
8. [ ] Add transactional receive-side descriptor-slot reservation and rollback.
9. [ ] Add QEMU tests for task exit, blocked receiver wakeup, and endpoint teardown.
10. [ ] Only then design ring-3 syscalls, user-memory preflight, syscall ABI versioning, and public service policy.

## Explicit non-goals

- No arbitrary PID-based channel access.
- No public global channel-index namespace.
- No raw kernel pointers in userspace IPC payloads.
- No ring-3 IPC or descriptor-transfer syscalls in the current increment.
- No shared-memory pointer exposure until per-task mapping authority and revocation exist.
- No claim that the current IPC implementation is safe for concurrent multi-core use.
