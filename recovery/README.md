# Recovery bundle (host-loss insurance)

Regenerate with `tools/recovery/export.sh` before every push. Contents:

- `darling/*.bundle`: the local-only commits of the Darling fork and each changed submodule, as git bundles (exact commits, merges included). `MANIFEST.txt` lists, for each repo, its HEAD and branch. It also lists any excluded import commit (Apple OSS tag or shallow upstream tip) that must be re-fetched first.
- `dyld-1378/dyld-1378.bundle`: dyl4 commits on top of the `apple-oss-distributions/dyld` tag `dyld-1378`.
- `notes/`: darling-recovery reports, darling-port-patches (owner's Oct 29 patches), handoff docs, cleanup report and agent memory.
- `tools/`: darling-port-tools and the dyld-port-a64 scripts.

No Apple binaries, caches, IPSW content or AEA keys are included.

## Restore (sketch)

```bash
git clone https://github.com/darlinghq/darling ~/darling && cd ~/darling
git fetch origin arm64-support
git bundle verify <bundle>                      # lists prerequisites to fetch
git fetch <path>/darling/_super.bundle HEAD:daysetta/arm64 && git checkout daysetta/arm64
git submodule update --init --recursive
# per submodule from MANIFEST: fetch the excluded import first when listed, e.g.
# xnu: git fetch https://github.com/apple-oss-distributions/xnu xnu-12377.121.6
cd src/external/<name> && git fetch <path>/darling/src__external__<name>.bundle HEAD:daysetta/arm64 && git checkout daysetta/arm64
# apply *.uncommitted.diff with git apply where present
```
