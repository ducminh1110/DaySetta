# ADR-0003 — Unix `SOCK_SEQPACKET` transport with a 32-byte `DSM1` header and `SCM_RIGHTS`

| Field | Value |
|---|---|
| Status | Accepted |
| Date | 2026-11-06 |
| Related | `adr/0002-language-cpp20.md`, `docs/DS1_WIRE.md`, `PROTOCOL_STRATEGY.md` |

## Context

The native control plane must deliver one framed message and its file descriptors as
one atomic unit. The receiver must never see half a header, a header from one message
paired with descriptors from another, or a descriptor count that disagrees with the
payload.

| Fact | Class | Evidence |
|---|---|---|
| The earliest design said "framed messages over a Unix socket" without fixing the socket type | `INFERRED` | `ARCHITECTURE.md:82-87` |
| The pre-implementation assumption was a byte-stream `SOCK_STREAM` socket | `INFERRED` | Design history; the repository text only says "Unix socket" |
| The implemented channel uses `AF_UNIX` `SOCK_SEQPACKET` for connect and listen | `IMPLEMENTED` | `src/ipc/ipc.cpp:62,84` |
| Descriptors are attached and received with `SCM_RIGHTS` | `IMPLEMENTED` | `src/ipc/ipc.cpp:146-155,191-198` |
| Peer identity is read with `SO_PEERCRED` | `IMPLEMENTED` | `src/ipc/ipc.cpp:114-123` |
| The header is exactly 32 bytes and the compiler enforces the size | `IMPLEMENTED` | `include/daysetta/protocol.hpp:19,81` |
| The wire contract documents the header offsets and the `DSM1` magic | `IMPLEMENTED` | `docs/DS1_WIRE.md:8-20` |
| The maximum message is 1 MiB and the maximum descriptor count is 64 | `IMPLEMENTED` | `include/daysetta/ipc.hpp:14-15` |

`SOCK_STREAM` would make framing a separate problem: a `read()` may return part of the
header, part of the payload, or two messages, and the ancillary descriptor list is
attached to a byte position rather than to a message. That forces a length-prefix state
machine and a second validation layer to prove that a descriptor list belongs to the
message it arrived with. `SOCK_SEQPACKET` preserves message boundaries, so one
`sendmsg()` is one datagram and one `recvmsg()` returns exactly one header-plus-payload
with its descriptors.

## Decision

1. **Socket type.** Use `AF_UNIX` + `SOCK_SEQPACKET` for native control channels.
2. **Framing.** Every datagram starts with the 32-byte `DSM1` header defined in
   `docs/DS1_WIRE.md` and `include/daysetta/protocol.hpp`.
3. **Descriptors.** Pass descriptors only as `SCM_RIGHTS` ancillary data on the same
   datagram. `handle_count` must equal the number of received descriptors.
4. **Atomicity.** A datagram is one request or one reply. A partial header, a size
   mismatch, a non-zero reserved field, or a descriptor-count mismatch is a protocol
   error and the peer is dropped.
5. **Limits.** Reject datagrams above 1 MiB and descriptor lists above 64 before
   allocating unbounded state.
6. **Identity.** Read `SO_PEERCRED` on accept and treat it as the only trusted peer
   identity; never trust a pid or uid from the payload.

## Consequences

* The deviation from `SOCK_STREAM` is deliberate and is now the contract. A future
  stream transport is a new ADR, not a silent fallback.
* `SOCK_SEQPACKET` is Linux-local. It does not cross a network and is not a replacement
  for a future shared-memory or `dma-buf` data plane.
* `SOCK_SEQPACKET` has a per-datagram size limit. The 1 MiB cap keeps a single message
  bounded; large pixel buffers travel as `memfd` descriptors, not as payload bytes.
* The broker still owns names and tokens. The transport carries messages; it does not
  grant authority.
* Tests must cover truncation, descriptor-count mismatch, oversized datagrams and
  short sends. Those tests do not exist yet; see `docs/IMPLEMENTATION_STATUS.md`.

## Rejected alternatives

| Alternative | Reason rejected |
|---|---|
| `SOCK_STREAM` | Requires a framing state machine and makes descriptor/message association ambiguous |
| `SOCK_DGRAM` | Unreliable; a lost control message cannot be retried safely at this layer |
| TCP loopback | Adds a network stack and port allocation to a per-user local service |
| A pipe per message | Descriptor and identity management become a second protocol |
