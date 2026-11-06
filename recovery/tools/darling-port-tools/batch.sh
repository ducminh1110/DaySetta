#!/bin/bash
# Run several BaseSystem binaries; each argument is "path args..."
for b in "$@"; do set -- $b; echo "=== $b"; DL4_LINES=${DL4_LINES:-8} DL4_TIMEOUT=${DL4_TIMEOUT:-40} bash $HOME/darling-port-tools/dl4.sh run "$@" 2>&1 | head -$(( ${DL4_LINES:-8} + 1 )); done
