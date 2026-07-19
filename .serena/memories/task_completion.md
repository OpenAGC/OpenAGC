# Task completion checklist

Before marking any coding task complete:

## 1. Build clean (generic)

```sh
rm -rf build
cmake -B build -DOPENAGC_PLATFORM=generic -DOPENAGC_BUILD_TESTS=ON
cmake --build build
```

## 2. Run tests

```sh
ctest --test-dir build --output-on-failure
# or: ./build/openagc_tests
```

Expected: **1483 passed, 0 failed**. Any drop is a regression — fix before declaring done.

> Note: `AGENTS.md`'s "Verification Checklist" mentions 772/772 — that number is stale. The live count (verified via `./build/openagc_tests`) is 1483. If you intentionally add tests, update this memory and `AGENTS.md` together.

## 3. Warnings

No new warnings under `-Wall -Wextra -Wpedantic` (already enabled in both CMake and Makefile).

## 4. New public symbols

- Declared in appropriate `include/*.h` with `PS5_SYSV_ABI` + Sony-style naming.
- Firmware-ABI struct → `_Static_assert` size/offset check.

## 5. New packet builders

- Test added in `tests/test_cb.c` or `tests/test_dcb.c` (header encoding + cursor advance).
- `STATUS.md` "Implemented Packet Builders" list updated.

## 6. (Optional) Prospero build

Only if you touched PS5 backend or ABI:
```sh
export PS5_PAYLOAD_SDK=~/ps5-payload-sdk
cmake -B build-prospero -DOPENAGC_PLATFORM=prospero -DOPENAGC_BUILD_TESTS=OFF \
    -DCMAKE_TOOLCHAIN_FILE=$PS5_PAYLOAD_SDK/toolchain/prospero.cmake
cmake --build build-prospero
```

## 7. Serena memory hygiene

If you edited memories: `serena memories check` from project root.