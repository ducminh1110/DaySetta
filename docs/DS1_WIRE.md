# ds/1 wire format (implementation contract)

Status: `IMPLEMENTED` once the M1 tests pass. This page is the authority for byte
layout; headers are the authority for C++ types. All integers are little-endian.

## 1. Framing

32-byte header, then `payload_len` bytes. The header is carried in
`include/daysetta/protocol.hpp` and is exactly:

| Offset | Width | Field |
|---|---|---|
| 0 | 4 | magic `DSM1` (`0x314d5344`) |
| 4 | 4 | total size, header included |
| 8 | 4 | op |
| 12 | 4 | flags (`reply=1`, `error=2`, `one_way=4`, `handle_bearing=8`) |
| 16 | 8 | sequence, monotonic per endpoint |
| 24 | 2 | handle count |
| 26 | 2 | reserved, must be zero |
| 28 | 4 | payload length |

A reply carries the request's op and sequence with `reply=1`. A typed error is a reply
with `error=1`; its payload is `u32 code` + `string detail`.

`string` is `u32 length` followed by that many bytes (no NUL, max 4096 in requests).
`rect` is four `i32`: x, y, w, h. `f32` is its IEEE-754 bits in a `u32`.

## 2. Operations

### hello (0x0001)

Request: `u32 client_version=1`, `u32 role` (0 app, 1 control, 2 test),
`string app_name`, `u64 broker_token`.

Reply: `u32 server_version=1`, `u32 session_id`, `u64 capabilities`,
`u32 max_message`, `u32 max_windows`.

Capability bits: 1 windows, 2 shared-memory surfaces, 4 events, 8 capture,
16 input injection. The token is the one the broker returned to the server at
registration; a mismatch is `Code::denied`.

### create_window (0x0010)

Request: `rect frame`, `i32 level`, `string title`. Reply: `u64 window_id`.

### destroy_window (0x0011)

Request: `u64 window_id`. Reply: empty.

### set_window_frame (0x0012)

Request: `u64 window_id`, `rect frame`. Reply: empty.

### set_window_visible (0x0013)

Request: `u64 window_id`, `u8 visible`. Reply: empty.

### order_window (0x0014)

Request: `u64 window_id`, `u32 order` (0 raise, 1 lower). Reply: empty.

### set_window_opacity (0x0015)

Request: `u64 window_id`, `u32 opacity_bits` (f32). Reply: empty.

### set_window_title (0x0016)

Request: `u64 window_id`, `string title`. Reply: empty.

### commit_surface (0x0020)

Request: `u64 window_id`, `u32 width`, `u32 height`, `u32 stride`, `u32 format`
(1 BGRA8888, 2 RGBA8888), `u64 surface_sequence`, `u32 damage_count`,
`damage_count * rect`, and exactly one descriptor: a memfd of at least
`stride * height` bytes. The server copies the pixels; the descriptor may be closed
when the reply arrives.

Reply: `u64 commit_sequence` (assigned by the server), `u32 accepted_damage`.

### subscribe_events (0x0030)

Request: empty. Reply: `u32 capacity`, `u32 event_size`, one descriptor: a sealed
event ring (`include/daysetta/event.hpp`).

### ack_events (0x0031)

Request: `u32 count`. Reply: empty. Informational only; the ring head is authoritative.

### scene_dump (0x0040)

Request: empty. Reply: `string json`.

### inject_input (0x0041)

Request: `u32 type`, `u32 flags`, `u64 timestamp_ns`, `u64 target_window`,
`i32 a`, `i32 b`, `i32 c`, `i32 d`. Reply: `u64 target_window` (0 when no window was hit).

### capture (0x0042)

Request: `u32 width`, `u32 height`. Reply: `u32 width`, `u32 height`, `u32 stride`,
`u32 format`, one descriptor: a memfd containing the BGRA frame.

### ping (0x00f0)

Request: `u64 token`. Reply: `u64 token`.

### status (0x00f1)

Request: empty. Reply: `string json` with counters and configuration.

## 3. Broker protocol (control socket)

The broker uses a separate 16-byte prefix: `u32 magic=DSK1 (0x314b5344)`, `u32 version=1`,
`u32 op`, `u32 status` (0 on success, `Code` otherwise), then
`string detail`, `string name`, `string path`, `u64 token`, `string json`.
Encoders/decoders live in `include/daysetta/sdk.hpp` (`broker_proto`).

Ops: `register_service=1`, `lookup_service=2`, `unregister_service=3`,
`list_services=4`, `ping=5`.

Rules:

* Only same-uid peers are accepted.
* Service names match `[A-Za-z][A-Za-z0-9._-]{0,127}`.
* A registered endpoint path must be absolute, reside under the runtime directory,
  and be a Unix socket owned by the caller's uid.
* `lookup` returns the path and the 64-bit token; every client presents the token at
  `hello`.
* `list` returns a JSON array of `{name,path,pid,uid}`; it never returns tokens.
