# dacoexec

[![ci](https://github.com/janagor/dacoexec/actions/workflows/ci.yml/badge.svg)](https://github.com/janagor/dacoexec/actions/workflows/ci.yml)
[![CodeQL](https://github.com/janagor/dacoexec/actions/workflows/codeql-analysis.yml/badge.svg)](https://github.com/janagor/dacoexec/actions/workflows/codeql-analysis.yml)

Header-only C++20 library. The public CMake target is `dacoexec::dacoexec`. It has no third-party dependencies.

```cmake
cmake_minimum_required(VERSION 3.29)
project(consumer LANGUAGES CXX)

find_package(dacoexec CONFIG REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE dacoexec::dacoexec)
```

```cpp
#include <dacoexec/dacoexec.hpp>
```

Linking the target requires C++20. Consumers may compile as C++20 or any later standard.

## Blocks

`dacoexec::views::blocks` is a lazy C++20 range adaptor. It partitions a byte input range into move-only blocks of at most N bytes. Each block owns its payload and a zero-based sequence number, so the block can outlive the source and be moved into later work.

```cpp
#include <dacoexec/dacoexec.hpp>

auto blocks = input | dacoexec::views::blocks(1 << 20);

for (auto block : blocks) {
  process(std::move(block));
}
```

The source is any range of `std::byte`, `char`, `unsigned char`, `signed char`, or `char8_t`, including move-only and single-pass ranges. A move-only lvalue view is passed with `std::move`, matching C++20 `viewable_range`. `dacoexec::views::blocks(n)(input)` is the same operation as the pipe.

`dacoexec::byte_block` is move-only. `sequence` is a `std::uint64_t` starting at zero. `bytes` is that block's `std::vector<std::byte>`. The final block may be shorter than N. Empty input yields no blocks. A block size of zero throws `std::invalid_argument`. Sequence numbers stop with `std::overflow_error` at the end of the `std::uint64_t` space.

Iteration is single-pass. Construction reads nothing. `begin()` reads at most the first block, and each increment reads only the next one. Dereference returns `byte_block&&`. A block can be moved out of the current position; a later dereference without advancing observes that moved-from value. Advancing invalidates references to the staged block. Blocks already moved into the caller stay valid after the view and the source are destroyed. Moving the view transfers its place in the input, including a block already staged. A moved-from view is empty.

The adaptor retains one staged block. That buffer starts at a capacity of at most 4096 bytes and doubles until it reaches N, so a short input does not allocate an N-byte block.

An exception from the source, including a failure while a block is only partly filled, propagates unchanged. The unfinished block is discarded.

## Install

```shell
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_STANDARD=20 \
  -DCMAKE_CXX_EXTENSIONS=OFF

cmake --build build
cmake --install build --prefix /tmp/dacoexec-install
```

The installed prefix contains `include/dacoexec/dacoexec.hpp` and `lib/cmake/dacoexec/` (or the platform `CMAKE_INSTALL_LIBDIR`). Pass that prefix as `CMAKE_PREFIX_PATH`.

## Use without installing

```cmake
add_subdirectory(path/to/dacoexec)
target_link_libraries(app PRIVATE dacoexec::dacoexec)
```

`FetchContent` works the same way. As a subproject, tests, installation, and developer tools stay off unless the parent turns them on. The embedded project does not change the parent's language mode or compiler flags.

## Build the tests

```shell
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_STANDARD=20 \
  -DCMAKE_CXX_STANDARD_REQUIRED=ON \
  -DCMAKE_CXX_EXTENSIONS=OFF \
  -Ddacoexec_BUILD_TESTS=ON

cmake --build build
ctest --test-dir build --output-on-failure
```

A core-only configure (`-Ddacoexec_BUILD_TESTS=OFF`) does not download Catch2 or stdexec.

## Options

| Option | Standalone | Subproject | Purpose |
| --- | --- | --- | --- |
| `dacoexec_BUILD_TESTS` | ON | OFF | Catch2 smoke tests and consumer fixtures |
| `dacoexec_BUILD_CLI` | OFF | OFF | Rejected until the production CLI exists |
| `dacoexec_BUILD_FUZZ_TESTS` | OFF | OFF | Rejected until codec fuzz targets exist |
| `dacoexec_BUILD_BENCHMARKS` | OFF | OFF | Rejected until benchmark targets exist |
| `dacoexec_INSTALL` | ON | OFF | Install rules for `dacoexec::dacoexec` |
| `dacoexec_WARNINGS_AS_ERRORS` | ON | OFF | Internal targets only |
| `dacoexec_ENABLE_CLANG_TIDY` | OFF | OFF | Developer or CI opt-in |
| `dacoexec_ENABLE_CPPCHECK` | OFF | OFF | Developer or CI opt-in |
| `dacoexec_ENABLE_FORMATTING` | OFF | OFF | Download formatting targets |

Sanitizers are also off until requested (`dacoexec_ENABLE_SANITIZER_ADDRESS`, `dacoexec_ENABLE_SANITIZER_UNDEFINED`, and the other `dacoexec_ENABLE_SANITIZER_*` options). Warning and sanitizer flags stay on the internal `dacoexec_options` and `dacoexec_warnings` targets. They are not part of the installed package.

## Development

Nix shells:

| Command | Compiler |
| --- | --- |
| `nix develop` | Clang 22 |
| `nix develop .#clang` | Clang 22 |
| `nix develop .#gcc` | GCC 16 |
| `nix develop .#clang16` | Clang 16 with its libc++ |
| `nix develop .#gcc12` | GCC 12 |

The existing presets `unixlike-clang-debug`, `unixlike-clang-release`, `unixlike-gcc-debug`, `unixlike-gcc-release`, and the Windows MSVC and clang-cl presets configure a strict C++20 build with tests enabled. See [Building](README_building.md).

## More details

* [Dependency setup](README_dependencies.md)
* [Building](README_building.md)
* [Troubleshooting](README_troubleshooting.md)
* [Docker](README_docker.md)
