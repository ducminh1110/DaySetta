#!/usr/bin/env bash
# DaySetta local/CI entry point.  This tree is still being implemented: a failure
# here is a real failure, not a reason to skip a step or fake a pass.
set -euo pipefail

cd "$(dirname "$0")/.."

JOBS="${JOBS:-8}"
status=0

echo "== DaySetta CI =="
echo "root: $(pwd)"
echo "jobs: ${JOBS}"
echo "NOTE: M0/M1/M2 are still open; this script reports the real build, test and docs result."

echo "== configure =="
if ! cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo; then
    echo "CI FAILED: configure failed" >&2
    exit 1
fi

echo "== build =="
if ! cmake --build build --parallel "${JOBS}"; then
    echo "CI FAILED: build failed (other work packages may still be incomplete)" >&2
    status=1
fi

if [ "${status}" -eq 0 ]; then
    echo "== ctest =="
    if ! ctest --test-dir build --output-on-failure; then
        echo "CI FAILED: ctest failed (the test suite is not complete yet)" >&2
        status=1
    fi
else
    echo "== ctest skipped: build did not succeed =="
fi

echo "== docs =="
if ! python3 ci/check_docs.py; then
    echo "CI FAILED: documentation check failed" >&2
    status=1
fi

if [ "${status}" -ne 0 ]; then
    echo "CI FAILED: one or more steps failed; no result was faked." >&2
    exit "${status}"
fi

echo "CI PASSED"
