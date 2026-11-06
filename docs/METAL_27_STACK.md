# Genuine macOS 27 Metal device path (arm64e, 26A434 IPSW cache)

Evidence classes: **O** = observed directly (symbol/string/class in extracted 27 binary); **D** = static disassembly constants, interpretation inferred (not decompiled, IOKit stubs unresolved); **R** = third-party reference source (Snapintosh, MIT, read as data); **U** = unverified hypothesis.
Binaries: `~/macos-re/m27/{Metal,IOGPU,AGXMetalG16X,AGXCompilerCore,libmetallinker.dylib,libAppleParavirt...}` extracted with `ipsw dyld extract --slide` from `~/macos-re/cache-arm64e`. Not committed (Apple binaries).

## 1. Device discovery and plugin load
| # | Claim | Class |
|---|---|---|
| 1 | `Metal.framework` exports `_MTLCreateSystemDefaultDevice`, `_MTLCopyAllDevices[WithObserver]`, `_MTLCopyDeviceForRegistryID`, `_MTLCreateDeviceWithID`, `_MTLCreateSimulatorDevice`; `_MTLCreateSystemDefaultDevice` calls `MTLDeviceArrayInitialize()` first (disasm @0x18d55755c) | O |
| 2 | Metal links `IOAccelerator.framework` and imports `IOAccelDeviceCreateWithAPIProperty`, `IOAccelCommandQueueCreateWithQoS`, `IOAccelCommandQueueSubmitCommandBuffers`, `IOAccelDeviceGet{Config64,MaxResourceSize,SharedMemorySize,SurfaceInfo,PeerInfo}`; Metal strings include `IOAccelerator`, `IOService`, `IOServicePublish`, `IOServiceCompatibility` | O |
| 3 | Metal strings `MetalPluginName` / `MetalPluginClassName`: the accelerator's registry properties name the driver bundle and its device class | O |
| 4 | A genuine driver plug-in is a bundle whose device class derives from `IOGPUMetalDevice` (SnapMetal: `AdrenoMTLDevice : IOGPUMetalDevice <MTLDeviceSPI>`; bundle path+class are read from IORegistry `MetalPluginName`/`MetalPluginClassName`, class via `-[NSBundle classNamed:]`, fallback `NSPrincipalClass`) | R |
| 5 | 27 ships 13 `AGXMetal*` bundles in `/System/Library/Extensions` (G13X…G19P; G16X/G17*/G18G/G19P present); their `Contents/MacOS` binaries live in the dyld cache, only `Archive_release*.metallib` (precompiled Apple shaders) sit in the cryptex | O |
| 6 | `AppleParavirtGPUMetalIOGPUFamily.bundle` (+ `libAppleParavirtCompilerPluginIOGPUFamily.dylib`) exists in the cache: Apple's own VM GPU plug-in, same IOGPUFamily model | O |
| 7 | `-[IOGPUMetalDevice initWithAcceleratorPort:options:]` calls `IOGPUDeviceCreateWithOptions` (fails -> nil) and creates shmem pools / dispatch queues / suballocator; `MTLAddDevice` asserts the concrete class adopts `MTLDeviceSPI` | R (+O: `IOGPUMetalDevice` ivar `_segmentByteThreshold`, `IOGPUMetalSuballocator*` symbols) |

Chain: `MTLCreateSystemDefaultDevice` -> `MTLDeviceArrayInitialize` -> IOKit match `IOAccelerator` (U: exact matching dict not decompiled) -> per service read `MetalPluginName/ClassName` -> load bundle, alloc class, `initWithAcceleratorPort:` -> `IOGPUDeviceCreateWithOptions` (user client open) -> `MTLAddDevice`.

## 2. IOGPU user-client surface
O: exports from `IOGPU.framework`: `IOGPUDevice{Create,CreateWithOptions,GetConfig[64],GetConnect,GetMemoryData,GetSharedMemorySize,GetSurfaceInfo,PerformMapping,CreateDeviceShmem,SetWaitForGPUCallback,…}`, `IOGPUResource{Create,CheckSysMem,…}`, `IOGPUCommandQueue{Create,SubmitCommandBuffers,SignalMTLEvent,WaitMTLEvent,Signal/WaitSharedEvent,SetPriorityAndBackground,SetQualityOfService,SetResourceGroups,DispatchAvailableCompletionNotifications}`, `IOGPUNotificationQueue{Create,WillSubmit,DidSubmit,…}`, ObjC `IOGPUMetal{Device,CommandQueue,CommandBuffer,Buffer,Texture,Heap,Resource,ResidencySet,Fence}`, `IOGPUMetalCommandBufferStorage{AllocResourceAtIndex,AllocSidebandBuffer,Begin/EndKernelCommands,BeginSegment,Grow*}`. Metal4 classes present (`IOGPUMetal4CommandQueue/CommandBuffer`). IOGPU imports `IOConnectCallStructMethod`, `IOConnectCallAsyncScalarMethod` (also in AGXMetalG16X).

D (immediates in the creating functions; mapping of immediate -> selector number NOT confirmed, IOKit stubs unresolved): `IOGPUDeviceCreateWithOptions` uses constants 0x11, 0x218, 0x40, 0x20 (struct sizes?); `IOGPUResourceCreate` immediate 9 (+0x10); `IOGPUCommandQueueCreate` immediates 7, 0x1c, 0x8001, 0x100; `IOGPUCommandQueueSubmitCommandBuffers` 0x1d (and 0x2c2). Hypothesis (U): selectors ~ {resource create 9, queue create 7, queue notify-register 0x1c, submit 0x1d}. Needs REA decompile with IOKit stub names before any fake user client is written.
Completion: command buffers complete via notification-queue Mach messages dispatched by `IOGPUNotificationQueueDispatch{Mach,Queue}` and `IOGPUCommandQueueDispatchAvailableCompletionNotifications` (O symbols); event stamps (R).

## 3. Shader path
| # | Claim | Class |
|---|---|---|
| 1 | Metal contains `MTLCompilerConnection`, `MTLCompilerConnectionManager[Internal]`, queue `com.apple.MTLCompilerConnectionQueue`, service name `com.apple.MTLCompilerService`, error text "Unable to reach MTLCompilerService…" | O |
| 2 | `_MTLCreateCompilerConnectionManager`, `_MTLGet{Default,Maximum,Optimal}CompilerProcessesCount` exported | O |
| 3 | Compiler stack in cache: `GPUCompiler.framework/Versions/32023/Libraries/{libGPUCompiler,libGPUCompilerImpl,libGPUCompilerUtils,libLLVM,libllvm-flatbuffers,libllvm-lmdb}.dylib`, `GPUCompiler/.../{libmetallinker,libsrc2module,libairutility,libLLVMContainer}`, `MTLCompiler.framework/…/libMTLCompilerHelper.dylib`, `MetalSerializer.framework`, `AGXCompilerCore.framework` (GPU-specific AIR -> AGX binary). The `MTLCompilerService` XPC executable itself is not in the cache and was not found in `store-27/root` | O (U: where the XPC binary lives) |
| 4 | Genuine 27 AIR: `air64_v29-apple-macosx27.0.0`, `Apple metal version 32023.921`, function constants (`air.fc_initializer`, `air.is_function_constant_defined`), `air.static_init`, TBAA, C++ mangled names (sample: SafariSharedUI `default.metallib`, 14770 B, magic `MTLB`) | O |
| 5 | Pipeline (inferred): MSL -> (MTLCompilerService/GPUCompiler frontend) -> AIR metallib; at PSO creation AIR -> AGXCompilerCore -> AGX binary (or precompiled `Archive_release*.metallib` in the AGX bundle). Env knobs: `MTL_DEBUG_IGNORE_PRECOMPILED_BINARIES`, `AGX_LOG_SHADER_COMPILER_REQUEST` | O (flow itself D/U) |
| 6 | A replacement driver substitutes the AIR -> hardware step with its own compiler in-process (SnapMetal: `air2ir3` AIR->NIR->ir3 library, `AdrenoShader`/`AdrenoCompilerInput`) | R |

## 4. Consequences for DaySetta
- Everything below `MTLDevice` is Apple userland that really talks IOKit; a plug-in subclassing `IOGPUMetal*` still needs `IOGPUDeviceCreate*`, resource and queue user-client calls answered (Darling IOKit shim; out of scope here, darlingserver not touched).
- Apple's shaders stay AIR; DaySetta must consume AIR itself (iridium) because the AGX compiler cannot run on Vulkan.
- Not verified: actual selector numbers, IORegistry match dictionary, whether genuine Metal tolerates a non-IOAccelerator device (SnapMetal shows it needs an IOGPUFamily accelerator service).

## 5. Translator test on genuine 27 AIR (this host)
`mtl2spv` (indium @68c2c18, built aarch64, LLVM 21.1.8) on genuine `SafariSharedUI.framework/…/default.metallib`: **SIGSEGV, no SPIR-V**. On `CoreImage.metallib`: "Invalid input file (not a Metal library)" (different container). On indium's own `add.metallib`: SIGSEGV; on `AAPLShaders.metallib`: `std::bad_alloc` (backtrace `Iridium::AIR::Function::analyze` after `LLVMGetElementType`). Cause (O, source): iridium uses `LLVMGetElementType` on pointer types and GEP/load result types (air.cpp 123-140, 428, 522, 1052-1100); LLVM >=17 has only opaque pointers (host has only LLVM 17-22). So no shader translation works on this host yet, for any AIR. Not run: spirv-val (no output to validate).
