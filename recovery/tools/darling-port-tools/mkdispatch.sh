#!/bin/bash
# Build libdispatch-apple and summarize errors.
cd ~/darling/build && cmake . > ~/darltest/cmake.log 2>&1
(make -k -j8 libdispatch_apple > ~/darltest/mkd.log 2>&1; echo make=$?)
grep -E "error:" ~/darltest/mkd.log | sed "s/.*error: //" | sort | uniq -c | sort -rn | head -${1:-25}
