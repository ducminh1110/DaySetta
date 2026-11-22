---
title: The boot hang was a scheduler bug
summary: Most boots stalled before login. An A/B swap of one binary pointed at the exact function, and the fix was a single condition.
tags: kernel, boot
---
While chasing the black screen, the boots themselves got worse: most of them stopped before login, stuck at `StartedAuthCopy` or `FinishLaunchingStart`. Out of 19 boots in a row, only 2 logged in.

## Following the stuck message

The logs showed the chain. `loginwindow` asks the HI services daemon a small question about the caps-lock key and waits for the answer. To answer, that daemon opens a HID connection to WindowServer. WindowServer accepts the connection and replies. Then the daemon sends its next two messages into the fresh connection port, the sends succeed, and **WindowServer never wakes up to read them.** Everything else in WindowServer keeps running; only that one port goes deaf.

That is a lost wakeup, the same family as bugs fixed earlier in the project, and those fixes were all still installed.

## One binary, two versions

So instead of guessing, the boots ran an A/B test with only the `darlingserver` binary swapped:

| darlingserver | logged in |
|---|---|
| the older build | 6 / 10 |
| the current build | 2 / 19 |

A function-by-function diff of the two binaries found exactly **one** functional change: `Thread::doWork`, the scheduler's resume path. An earlier fix for a resume racing a suspend set a "resume pending" marker unconditionally. A stray resume on a thread that was running, not parking, left the marker behind; a later genuine suspend ate it and skipped its park, and a blocking receive returned without ever waiting. A lost wakeup, manufactured by the fix for another lost wakeup.

## The fix

Only remember the pending resume when the thread is really parking:

```cpp
if (_running) {
    // only when actually parking
    if (_suspended) {
        _resumePending = true;
    }
    ...
}
```

A host test reproduces the poison red and goes green with the condition; the earlier park-race test still passes. On real boots, logins went from 2 / 19 to 4 / 10.

## Not done yet

About half the boots still hang, and this time it is not the port layer: port logging shows every message arriving and being read. In the stuck boots, a libdispatch event handler (a connection invalidation callback) simply never runs, so `loginwindow` waits forever on a semaphore nobody signals. That is the next hunt: the guest's workqueue emulation, where work gets queued but never executed.
