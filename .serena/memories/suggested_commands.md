# Suggested commands

## Host build & test (primary verification)

```sh
# CMake (clean)
rm -rf build && cmake -B build -DOPENAGC_PLATFORM=generic -DOPENAGC_BUILD_TESTS=ON
 cmake --build build
ctest --test-dir build --output-on-failure

# Make (fast)
make            # libopenagc.a
make test       # builds + runs openagc_tests
make -B test    # force rebuild then test
make clean
```

Test binary itself: `./build/openagc_tests` or `./openagc_tests`. Expected: **1483 passed, 0 failed**. (Note: `AGENTS.md` mentions 772/772 in its checklist — that count is stale; the live number is 1483.)

## PS5 (prospero) build

```sh
export PS5_PAYLOAD_SDK=~/ps5-payload-sdk
cmake -B build-prospero -DOPENAGC_PLATFORM=prospero -DOPENAGC_BUILD_TESTS=OFF \
    -DCMAKE_TOOLCHAIN_FILE=$PS5_PAYLOAD_SDK/toolchain/prospero.cmake
cmake --build build-prospero
# → build-prospero/libopenagc.a
```

## Hardware samples

```sh
cd samples/hw_test
export PS5_PAYLOAD_SDK=~/ps5-payload-sdk
make all                       # videoout_linear.elf + agc_init.elf
make videoout_linear.bin agc_init.bin   # fake-SELF
make deploy_videoout           # prospero-deploy videoout_linear.elf
make deploy_agc                # prospero-deploy agc_init.elf
make install_videoout          # FTP upload as eboot.bin (debug-mode)
make install_agc
```

## Darwin specifics

- `make`/`cmake`/`ctest` behave as standard. Default `cc` is Apple clang — accepts `-Wall -Wextra -Wpedantic`.
- For prospero cross-build on macOS: `brew install llvm@18 wget` then `export LLVM_CONFIG=/opt/homebrew/opt/llvm@18/bin/llvm-config`.
- No GNU-specific flags used; no `sed -i ''` quirks needed in this repo.

## Serena

- `serena memories check` from project root — sanity-check memory references after edits.