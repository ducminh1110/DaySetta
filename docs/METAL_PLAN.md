# DaySetta Metal plan (M1 output)

Rules: genuine Metal.framework + app-facing APIs stay Apple's. Never call software rendering "Metal support". Every artifact is labelled hardware or llvmpipe/lavapipe.

## Host reality (docs/HOST_GPU.md)
virtio-gpu 2D only (3D_FEATURES=0, no blob/capsets): no virgl, no Venus. Vulkan = lavapipe (llvmpipe, SOFTWARE). Hardware acceleration is impossible on this host; the path is designed for a Vulkan device and proven only on lavapipe here. aarch64 and x86_64 are the same code (Vulkan + LLVM, no arch-specific parts).

## Backend decision (owner decision, 2026-11-06): indium
Indium (Darling, `github.com/darlinghq/indium` main @68c2c18, local branch `daysetta/metal`) is the Metal-on-Vulkan backend; iridium/mtl2spv is the AIR->SPIR-V translator. metal2vulkan is reference only, not integrated.

Strategy comparison (evidence: METAL_27_STACK.md):
| Option | Verdict |
|---|---|
| (a) fake AGX/IOGPU device translating to Vulkan | Rejected as the *backend*: would emulate AGX command streams. A thin fake IOGPU user client is still needed *under* (b). |
| **(b) DaySetta MTLDevice plug-in (`DaySettaMetal.bundle`, device class `: IOGPUMetalDevice`, as AGXMetal and SnapMetal do) mapping MTL* -> indium -> Vulkan** | **Chosen.** Genuine Metal loads it via `MetalPluginName/ClassName`; genuine IOGPUMetal*/_MTL* base classes and all app-facing API remain Apple's; only encoding/submission/shader compile are replaced. Precedent: Snapintosh SnapMetal (R), Apple Paravirt plug-in (O). |
| (c) replace Metal.framework by an indium-based reimplementation (Darling's original goal) | Rejected: violates "genuine Metal.framework" and 27 Metal has Metal4/ICB/residency APIs indium lacks. |
Required companion: a minimal fake IOAccelerator service + IOGPU user client in Darling's IOKit layer (device create/config/shmem, resource create, queue create, submit as a fence-signalling no-op, notification queue). Owned by whoever owns IOKit/darlingserver; selector numbers must first be confirmed (STACK section 2).

Reference comparison (verified on clones in ~/ref, treated as data, nothing executed/built):
| | indium | metal2vulkan (steelbrain) | Snapintosh |
|---|---|---|---|
| Language / licence | C++17, 0BSD-text (ISC-family; LICENSES/ in metal repo has ISC) | Rust, GPL-2.0+ OR LGPL-3.0+ (LICENSE verified) | air2ir3/SnapMetal MIT |
| Activity | main @68c2c18 2026-11-04 (not dormant; lead's "2023-04" was the pinned submodule commit 8423a7d) | HEAD 2026-10-29; commit count not verifiable (shallow clone) | Oct 2026 |
| Shape | Metal-like C++ API on Vulkan 1.3 + iridium (AIR->SPIR-V, uses LLVM C API via dlopen) + mtl2spv; ~6.7k LOC | Translator crate + runtime on `ash` (Vulkan 1.2, timeline sem.) + own AIR bitcode reader (`metal2vulkan-air`, no LLVM dependency), typed refusals, claims validation vs real Metal; ~1.7M lines incl. corpus; coverage doc: 958 implemented / 828 missing of 2135 Metal 27 items | AIR->NIR->ir3 for Adreno only (not Vulkan); plug-in shows the genuine-driver-bundle pattern |
| Builds here | yes (aarch64, with `-include stdexcept/cstdint/cstring`) | not attempted: needs Rust 1.98.1, host has 1.93; build.rs would execute repo code | not built |
| Fit | already in Darling, C++ fits an ObjC plug-in | Rust + GPL/LGPL linking into a plug-in is a licence and toolchain burden; its design (own bitcode reader, refusals) answers indium's blocker | pattern reference only |
| Hypothesis "use metal2vulkan translator + indium runtime" | not tested: owner decision forbids integration. Both translators were NOT run on the same AIR; only mtl2spv was run (fails, STACK s.5). | | |

## Milestones and acceptance tests
- **M-1 Iridium on modern LLVM (blocker, first).** Port iridium off typed pointers: pointee from GEP source type / call types / AIR metadata (`air.arg_type_name`, `air.struct_type_info`), GEP result-type computation, `LLVMGlobalGetValueType` for function type. Alternative to evaluate: a small own AIR bitcode reader (metal2vulkan-air as design reference only). Accept: `mtl2spv` on `AAPLShaders.metallib` and `add.metallib` emits SPIR-V that passes `spirv-val`; then on genuine 27 `default.metallib` (air64_v29) emits or refuses with a typed reason, never crashes.
- **M0 Indium visual smoke (done).** `offscreen-triangle` (src/metal) -> clear + blit readback -> PNG on lavapipe: artifacts/screenshots/metal-indium-clear-llvmpipe-SOFTWARE-aarch64.png, exit 0. Accept on x86_64: same binary source, same PASS line.
- **M1 Indium triangle + compute.** `DAYSETTA_TRY_AIR=1 offscreen-triangle` draws the triangle (centre pixel not background); `indium-test-basic-compute` prints "Compute results as expected". Software label retained.
- **M2 Confirm IOGPU user-client contract.** REA decompile of `IOGPUDeviceCreateWithOptions`, `IOGPUResourceCreate`, `IOGPUCommandQueueCreate/Submit` with IOKit stub names; table of selectors, struct layouts, shmem mapping. Accept: STACK section 2 has no D/U rows.
- **M3 Fake IOAccelerator + IOGPU user client (Darling IOKit).** Accept: genuine `IOGPUDeviceCreateWithOptions` returns a device; `IOGPUMetalDevice initWithAcceleratorPort:` succeeds with a stub subclass.
- **M4 DaySettaMetal.bundle skeleton.** `MTLCreateSystemDefaultDevice()` from genuine Metal.framework returns the DaySetta device (name labelled `... (Vulkan: llvmpipe, SOFTWARE)`), `newCommandQueue`, `newBuffer` round trip. Accept: small genuine-ABI test app (arm64e) prints device name and buffer contents.
- **M5 Compute + blit through the plug-in** (`MTLComputePipelineState` from a genuine metallib via iridium; compare with CPU reference).
- **M6 Render pass + texture readback**; **M7 CAMetalLayer/IOSurface present** into the windowserver (coordinate with src/windowserver); **M8 MSL source compile** (`newLibraryWithSource` needs MSL->AIR: Apple's GPUCompiler frontend in the cache or MTLCompilerService substitute); **M9 Metal4/ICB/residency sets**; **M10 real Vulkan hardware** (a host with virgl/Venus or native GPU) re-running M5-M7 with label "hardware".

## Build notes
`cmake -S src/metal -B build-metal/smoke -G Ninja && ninja -C build-metal/smoke offscreen-triangle`; run with `VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json` (the default ICD set tries freedreno first and prints a harmless TU error because ubuntu is not in group `render`). Needs: libvulkan-dev, llvm-dev (iridium dlopens libLLVM), libpng-dev, xxd.
