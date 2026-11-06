---
name: rea-reverse-engineering-setup
description: "Where REA (rea-agents MCP), Ghidra, Node 24 and DMG/Mach-O tools are installed on this aarch64 Ubuntu host, and their quirks"
metadata:
  node_type: memory
  type: reference
  originSessionId: fa91baf1-5dd6-4aeb-8211-2202c0fc70d0
  modified: 2026-10-29T09:13:00.001Z
---

Installed 2026-10-29 for the Darling macOS-port work ([[darling-xnu-port]]); the user plans to reverse-engineer Apple's BaseSystem.dmg.

- Node 24.22.2 (official tarball): `~/.local/node/bin` — system `/usr/bin/node` is 22.22.1, too old for REA (needs >=22.23.4 / 24.22.1 / 26.12.1). PATH export added to ~/.profile and ~/.bashrc.
- REA CLI `rea` 6.8.0 (npm global into ~/.local/node). MCP server `rea` in `~/.claude.json` is pinned to `~/.local/node/bin/node .../rea-agents/scripts/rea.mjs mcp` because `rea`'s `#!/usr/bin/env node` shebang would pick the old system node. Re-check this pin after `rea update` / `rea setup`. Skill at `~/.claude/skills/reverse-engineer-anything`.
- Ghidra 12.1.4 at `~/.local/opt/ghidra_12.1.4_PUBLIC` (GHIDRA_INSTALL_DIR), JDK 21 at /usr/lib/jvm/java-21-openjdk-arm64. Release ships no linux_arm_64 natives; they were built with `support/gradle/gradlew buildNatives` — redo this after any Ghidra upgrade.
- `rea extract-artifact` cannot open DMG. Use 7-Zip 26 (`7z`, supports Dmg/APFS/HFS), `dmg2img`; Mach-O inspection via `llvm-otool`, `llvm-objdump`, `clang -target x86_64-apple-macos*`, `ld64.lld`.
- Not installed: frida install script (blocked by npm allow-scripts), rizin and apfs-fuse (not in Ubuntu 26.04 apt).
