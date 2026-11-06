# AGENTS — DaySetta implementation rules

Read this before touching code.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j8
./build/daysetta_tests            # all cases
./build/daysetta_tests scene      # filter by substring
```

Toolchain: C++20, GCC 15 or Clang 21, CMake 3.20+. No external libraries may be
added. `-Wall -Wextra -Wpedantic -Werror` is enforced by CMake.

## Ownership

* `include/daysetta/*.hpp` and `CMakeLists.txt` are interface files. A work package
  must not edit them unless its brief explicitly says so.
* Tests are one executable. Add `tests/test_<area>.cpp` with `DS_TEST(name) { ... }`
  and `DS_CHECK`/`DS_CHECK_EQ`; never add a `main`.
* Do not edit files outside your assigned work package.
* Do not commit Apple binaries, dylibs or cache files.

## Evidence

* Code comments explain observed behaviour, not plans.
* A claim in documentation carries an evidence class and a reference. If you could
  not test something, write `UNVERIFIED` and say what is missing.
* Never invent an Apple constant, selector, MIG id or line number.

## Current wire contract

`docs/DS1_WIRE.md` is authoritative for bytes. `include/daysetta/*.hpp` is
authoritative for C++ signatures.
