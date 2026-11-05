# Glossary

> Status: `PLANNED` where a term describes DaySetta. Terms about Daylight or Apple are
> definitions, not implementation claims.

## 1. Projects and substrates

| Term | Meaning |
|---|---|
| **Daylight** | The predecessor project: an open reimplementation of the macOS window server that ran inside Darling (`IMPLEMENTED` for its implemented subset) |
| **DaySetta** | The planned successor described by this repository |
| **Darling** | A Linux-hosted Darwin/macOS compatibility layer. DaySetta does not run inside it |
| **Substrate** | DaySetta's own compatibility layer for unmodified macOS binaries; not Darling |
| **Native profile** | DaySetta profile for Linux ELF clients using the `ds/1` protocol |
| **Compatibility profile** | DaySetta profile that additionally serves the SkyLight-shaped dialect |
| **Clean-room** | Reimplementation from documented behaviour and evidence, without copying Apple code or binaries |
| **Bring-your-own-artifact** | A user supplies a macOS binary or cache for analysis; it is never redistributed by DaySetta |

## 2. Apple platform terms

| Term | Meaning |
|---|---|
| **Mach** | The IPC and virtual-memory substrate used by macOS; DaySetta emulates the message shape in user space, not the kernel |
| **Mach port** | A kernel object denoting a communication endpoint and its rights. DaySetta replaces it with a broker-issued handle |
| **Port right** | Permission to send to, receive from, or manipulate a port. Daylight used dispositions 17, 19 and 20 (`VERIFIED`) |
| **Bootstrap** | The name service used by macOS clients to find services. DaySetta's broker plays this role at the API boundary |
| **MIG** | Mach Interface Generator: the tool and convention that turn interface definitions into message stubs |
| **NDR** | Network Data Representation: the type encoding used inside MIG messages |
| **SkyLight** | The private framework and protocol family behind the macOS window server |
| **CGX** | The subsystem prefix used by the SkyLight MIG inventory, for example `CGXServices` and `CGXWindowServer` |
| **WindowServer** | Apple's window server process; the role Daylight and DaySetta reimplement |
| **CARenderServer** | The Core Animation render-server service; Daylight's CA daemon registered `com.apple.CARenderServer` (`IMPLEMENTED`) |
| **QuartzCore** | The framework containing Core Animation and the `CA::Render` machinery |
| **AppKit** | The macOS application framework whose windows Daylight drove |
| **CoreGraphics** | The drawing and event-encoding framework used by AppKit clients |
| **CASDFLayer** | A QuartzCore layer type used by the Dock's glass floor; Daylight observed it and could not draw it with `renderInContext:` |
| **SDF** | Signed distance field: a scalar field used to evaluate shape coverage and material masks |
| **XPC** | Apple's service IPC mechanism, launched by `launchd`. Absent in the Daylight guest and out of scope for DaySetta |
| **launchd** | Apple's service manager. DaySetta has no equivalent service graph |
| **dyld** | Apple's dynamic loader. DaySetta does not depend on it |
| **dyld shared cache** | A prelinked collection of macOS images. DaySetta reads it offline only, never executes from it |
| **Mach-O** | The executable format used by macOS |
| **UUID** | The unique identifier of a Mach-O image, used by the catalog to identify releases |

## 3. DaySetta components

| Term | Meaning |
|---|---|
| **`daysetta-broker`** | Per-user endpoint namespace and capability issuer |
| **`daysetta-server`** | The window server: scene, surfaces, input, display, compositing |
| **`daysetta-session`** | Per-login environment and server lifecycle |
| **`daysetta-ctl`** | Inspection and control tool |
| **`daysetta-catalog`** | Offline artifact reader and catalog builder |
| **`ds-ipc`** | Message framing, handles, rights, fd passing and peer checks |
| **`ds-protocol`** | IDL, NDR/MIG subset, validation and generated stubs |
| **`ds-compat`** | Adapter from the SkyLight-shaped dialect to the server core |
| **`ds-loader`** | ELF and Mach-O mapping, binding and TLS |
| **`ds-rt`** | libSystem-shaped ABI and Objective-C runtime for the substrate |
| **`ds-scene`** | Windows, spaces, levels, ordering and notes |
| **`ds-surface`** | Surface slots, damage and sequence validation |
| **`ds-compositor`** | Composition of scene layers into a frame |
| **`ds-render`** | Render graph and CPU/Vulkan backends |
| **`ds-material`** | SDF shapes, backdrop sampling, blur, tint, highlight, border |
| **`ds-input`** | Device normalization and event routing |
| **`ds-display`** | Display topology, modes and frame pacing |
| **`ds-sdk`** | Native client API |

## 4. Protocol terms

| Term | Meaning |
|---|---|
| **`ds/1`** | DaySetta's native protocol, versioned by major |
| **`skylight-26.6`** | The compatibility dialect pinned to the macOS 26.6 SkyLight inventory |
| **Dialect** | A named mapping from protocol operations to server-core behaviour |
| **Routine** | One MIG operation in the SkyLight inventory; 738 were catalogued in 9 subsystems |
| **Reply id offset** | The observed convention that a reply id is the request id plus 100 (`VERIFIED`) |
| **Canned reply** | A reply recorded from a real server and replayed. Daylight used them; DaySetta removes them from production |
| **Session note** | An asynchronous server-to-client state message; Daylight used `0x390`, `0x391`, `0x33b` |
| **Handle** | A broker-issued reference to an endpoint or resource, with explicit rights |
| **Capability** | A grant that authorises a specific operation on a handle |
| **Explicit unsupported** | A typed refusal naming the operation, instead of a fabricated success |
| **Trace replay** | Re-running a normalized recording against a server under test |

## 5. Rendering terms

| Term | Meaning |
|---|---|
| **Scene graph** | The server's model of spaces, windows and layers |
| **Surface** | A client-owned pixel buffer with format, stride and damage metadata |
| **Slot** | One buffer in a surface's rotating set |
| **Damage** | The region of a surface or window that changed and must be recomposited |
| **Frame snapshot** | An immutable scene state handed to the render thread |
| **Render graph** | A per-frame DAG of passes with declared reads and writes |
| **Material** | A compositor primitive that samples a backdrop and applies blur, tint, highlight and border |
| **Backdrop** | The already-composited content behind a material |
| **Material floor** | The documented fallback when a backend cannot evaluate a material |
| **Golden frame** | A reference frame with a recorded hash and renderer version |
| **Premultiplied alpha** | Colour channels already multiplied by alpha, used at every API boundary |

## 6. Evidence and testing terms

| Term | Meaning |
|---|---|
| **Evidence class** | One of `VERIFIED`, `IMPLEMENTED`, `PARTIAL`, `OBSERVED`, `INFERRED`, `UNKNOWN`, `PLANNED` |
| **Catalog** | The machine-readable record of images, exports, ObjC metadata and MIG routines derived from artifacts |
| **Export trie** | The dyld structure that encodes a Mach-O image's exported symbols |
| **Pointer salt** | High bits in cache-internal pointers that encode kind or offset information; partially decoded (`OBSERVED`) |
| **Fixture** | A test input with recorded provenance |
| **Conformance** | Agreement with a defined contract, not with an Apple implementation's internals |
| **Tier** | A compatibility claim level: T0 native, T1 source-shaped, T2 unmodified binary, T3 system services |
| **Envelope** | The measured dependency and feature boundary for a T2 application class |
| **Audit file** | A generated record of tiers, modelled routines, fixtures and exclusions for a release |
