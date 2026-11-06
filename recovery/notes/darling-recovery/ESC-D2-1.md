# ESC-D2-1
## Goal
Task 4: run the genuine Apple arm64 (non-e) slice of store-27 /bin/echo inside Darling on this aarch64 host.
## Done
Darling aarch64 (daysetta/arm64) built, installed (/usr/local), D2-run.txt transcripts OK for Darling-built arm64 binaries.
## Blocker (verbatim)
`darling shell <slice> hi` -> rc=132; strace: `--- SIGILL {si_signo=SIGILL, si_code=ILL_ILLOPC, si_addr=0x200057790} ---` for slice1 (arm64e), slice2 (arm64, cpusubtype 0xc) and the fat file.
## Tried
Disassembled: si_addr is Darling dyld (/usr/local/libexec/darling/usr/lib/dyld+0x57790) `pacia x0, x1` in dyld3::MachOLoaded::ChainedFixupPointerOnDisk::Arm64e::signPointer. slice2 has pointer_format 12 (ARM64E_USERLAND24), __auth_stubs, `pacibsppc`, `braa`; it differs from the arm64e slice only by cpusubtype/signature (cmp -l: 380 bytes).
## Evidence
/home/ubuntu/darling-recovery/D2-run.txt (tail); scratch slices in /tmp/claude-1001/-home-ubuntu/0b182f4a-fdf5-457c-bf1f-27afc98a81e7/scratchpad/apple (outside git); host Features: fp asimd aes pmull sha1 sha2 crc32 cpuid (no paca/pacg/atomics).
## Hypotheses
macOS 27 ships no non-PAC arm64 slice; any genuine 27 binary needs PAC lowering (ds-exec at /home/ubuntu/daysetta/src/exec) at load time, plus Darling dyld's own PAC instructions (signPointer/auth paths) would still SIGILL unless dyld is also built non-PAC or fixups pre-lowered.
## Question
Proceed with ds-exec lowering for Darling (pre-rewrite Mach-O to strip PAC + chained-fixup format 12 -> plain before mldr loads it), or also rebuild Darling dyld without PAC paths?
