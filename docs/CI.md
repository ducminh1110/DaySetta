# CI

Local entry point: `bash ci/run.sh`. It is the same sequence the GitHub workflow
runs, with a different working directory.

## 1. What `ci/run.sh` does

| Step | Command | Failure behaviour |
|---|---|---|
| Configure | `cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo` | Exit 1 immediately |
| Build | `cmake --build build --parallel "${JOBS:-8}"` | Record failure, skip ctest, still run docs |
| Test | `ctest --test-dir build --output-on-failure` | Record failure, still run docs |
| Docs | `python3 ci/check_docs.py` | Record failure |
| Result | Print `CI FAILED` and exit nonzero if any step failed | Never prints a fake pass |

`set -euo pipefail` is active. `JOBS` overrides the build parallelism. The tree has a
green native prototype but M0/M1/M2 are still open, so a red run is a real failure;
the script says which step failed and never converts a failure to success. At
**2026-11-06T08:21Z** it printed `CI PASSED` with 22/22 cases (`VERIFIED`).

## 2. What `ci/check_docs.py` checks

The checker uses only the Python standard library.

| Check | Rule |
|---|---|
| Markdown links | Every relative link and image target must exist; `http`, `https`, `mailto`, `ftp`, `data` and `tel` are skipped; fragments are ignored for file resolution |
| `path:line` citations | A backticked `path:line` or `path:line-line` must resolve to a file and the line range must be within that file |
| Evidence tree | Relative paths resolve against the repository first, then `/home/ubuntu/daylight`; `docs/knowledge/...` and `data/apple-api/...` therefore resolve when the evidence tree is present |
| Missing evidence tree | If `/home/ubuntu/daylight` is absent, external citations are warnings, not errors; repository links are still errors when broken |
| Evidence classes | Every `adr/*.md` file and `docs/IMPLEMENTATION_STATUS.md` must contain at least one of `VERIFIED`, `IMPLEMENTED`, `PARTIAL`, `OBSERVED`, `INFERRED`, `UNKNOWN`, `PLANNED` |
| Handoff budget | `AI_AGENT_HANDOFF.md` must have at most 60 lines |
| File size | Any repository file above 2 MiB is rejected, except `.git`, `build/` and `build-*` output |
| macOS 27 artifact | If `artifacts/macos-27/mig-routines.json` exists, it must parse with `json.load` and have a JSON object at the top level |
| Summary | Prints counts and every warning/error; exit 1 when there is at least one error |

The evidence root can be overridden with `DAYSETTA_EVIDENCE_DIR`.

## 3. GitHub workflow

`.github/workflows/ci.yml` runs on `ubuntu-latest`:

1. Checkout.
2. Print the CMake and GCC versions.
3. Configure, build and run ctest.
4. Run `python3 ci/check_docs.py` even if ctest failed.
5. Fail the job if ctest failed.

There is no Apple artifact download, no macOS runner, no package installation and no
network step beyond the checkout action. The test step uses `continue-on-error` only
so the documentation check still runs; a later step fails the job when ctest failed.

## 4. Running one check directly

```sh
python3 ci/check_docs.py
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j8
ctest --test-dir build --output-on-failure
./build/daysetta_tests
```

A documentation failure is an error in a file, not a reason to weaken the checker.
Fix the citation or the link, or move an out-of-scope failure to
`docs/IMPLEMENTATION_STATUS.md` with an explicit owner.

## 5. Current limitations

* The checker validates that a line number is inside the file; it does not prove that
  the cited line still says what the document claims. Reviewers do that.
* Backticked citations without a line number are not checked.
* Mermaid blocks are not parsed; only Markdown links and backticked citations are.
* `artifacts/macos-27/mig-routines.json` is optional and absent from this repository.
  The check becomes active when a local artifact is added.
