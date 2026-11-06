# ESC-G1-1: no cross-process memory in the installed Darling (darlingserver RPCs down)
Seen twice (G1, windowserver):
1. `fileport_makeport` -> libsystem_kernel prints "Something called the old LKM API (nr = 4158)" and aborts.
2. `mach_make_memory_entry_64` -> "*** dserver_rpc_interrupt_enter failed with code -111 ***" (loops, hangs).
`darling_memory_entry_create` is absent from the arm64 libsystem_kernel.
Effect: SkyLight shmem regions (event scoreboard, display shmem, window state 0xb0, connection 0x40) and AttachCanvas
canvases cannot be shared as Mach rights. Workaround in src/windowserver/src/main_darling.c: host-file-backed region +
placeholder port (probe client maps by path). Genuine 27 AppKit needs the real memory-entry RPC (darlingserver; owner restores).
Not touched: darlingserver.
