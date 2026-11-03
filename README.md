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

`dacoexec::byte_block` is move-only. `sequence_` is a `std::uint64_t` starting at zero. `bytes_` is that block's `std::vector<std::byte>`. The final block may be shorter than N. Empty input yields no blocks. A block size of zero throws `std::invalid_argument`. Sequence numbers stop with `std::overflow_error` at the end of the `std::uint64_t` space.

Iteration is single-pass. Construction reads nothing. `begin()` reads at most the first block, and each increment reads only the next one. Dereference returns `byte_block&&`. A block can be moved out of the current position; a later dereference without advancing observes that moved-from value. Advancing invalidates references to the staged block. Blocks already moved into the caller stay valid after the view and the source are destroyed. Moving the view transfers its place in the input, including a block already staged. A moved-from view is empty.

The adaptor retains one staged block. That buffer starts at a capacity of at most 4096 bytes and doubles until it reaches N, so a short input does not allocate an N-byte block.

An exception from the source, including a failure while a block is only partly filled, propagates unchanged. The unfinished block is discarded.

## Buffered bytes

`dacoexec::views::buffered_bytes` reads a byte input range in fixed windows and yields one `std::byte` at a time. A byte sequence continues from the end of one window into the next.

```cpp
auto bytes = source | dacoexec::views::buffered_bytes(4096);

for (std::byte value : bytes) {
  consume(value);
}
```

`dacoexec::views::buffered_bytes(n)(source)` is the same operation. The source is any `dacoexec::byte_input_range`, including move-only and single-pass ranges. A buffer size of zero throws `std::invalid_argument`. Empty input yields no bytes. The final window may be shorter than the buffer.

Iteration is single-pass. Construction reads nothing. `begin()` fills only the first window, and the next window is read only after that one is consumed. Copying an iterator aliases that same pass. Moving the view keeps its place, including a window that is only partly consumed. A moved-from view is empty.

The window starts at a capacity of at most 4096 bytes and doubles until it reaches the requested size. A short or empty source does not allocate the full buffer.

An exception from the source propagates unchanged. The unfinished window is discarded, and later iteration does not resume.

## Bits

`dacoexec::views::bits` is a lazy range adaptor. It yields each byte as eight `bool` values, most significant bit first. The order does not depend on the host endianness, and the adaptor does not allocate a bit buffer.

```cpp
std::vector<std::byte> bytes{std::byte{0xB2}, std::byte{0x80}};

auto bits = bytes | dacoexec::views::bits();
// 1 0 1 1 0 0 1 0  1 0 0 0 0 0 0 0
```

`dacoexec::views::bits()(bytes)` is the same operation. The source is any `dacoexec::byte_input_range`, including move-only and single-pass ranges. A move-only lvalue view is passed with `std::move`. An empty byte range yields no bits. Each byte contributes exactly eight bits.

Iteration is single-pass. Construction reads nothing. `begin()` caches the first source byte, and the underlying iterator advances only after those eight bits are consumed. Copying a bit iterator aliases that same pass. Moving the view keeps its place in the input, including a byte already cached. Destroying the view that owns the iteration state invalidates its iterators. A moved-from view is empty. An lvalue source is borrowed. An rvalue source is owned for the life of the view.

An exception from the source propagates unchanged. The view does not turn that failure into an end-of-stream, and later iteration does not resume.

`dacoexec::bit_writer` packs those bits into any output iterator of `std::byte`. `write_bit` writes one bit. `write_bits(value, n)` writes the least significant `n` bits of `value`, most significant of those bits first. `n` is 0 through 32. A zero-length write of value 0 does nothing. A count above 32, or a value with bits set above that width, throws `std::invalid_argument`. A finalized or failed writer throws `std::logic_error` before those argument checks.

The writer keeps a single partial byte. Filled bits sit in the high positions, and a full byte is emitted immediately. `finish()` writes a trailing partial byte padded with zero bits on the right and returns how many padding bits were added (0 through 7). Calling `finish()` again returns the same count and writes nothing. The destructor does not pad or flush. Writing after `finish()`, or after the output iterator throws, throws `std::logic_error`. An output failure propagates as the original exception. Bytes the sink already accepted stay written, and the failed byte is not retried.

Moving a writer transfers its partial byte and its output iterator. Move assignment discards the destination writer's previous partial byte without writing it. If moving the output iterator throws, both writers become unusable and neither emits its partial byte.

```cpp
std::vector<std::byte> encoded;
dacoexec::bit_writer writer{std::back_inserter(encoded)};

writer.write_bits(0b101u, 3);
writer.write_bits(0b11001u, 5);
auto const padding = writer.finish();// encoded == {0xB9}, padding == 0
```

`writer.output_iterator()` is a C++20 output iterator of `bool`. Assignment writes one bit. Copies share the writer's packing state, and incrementing a copy does not emit a byte. The iterator must not outlive the writer. Moving the writer invalidates its output iterators.

A complete byte sequence round-trips: reading it with `views::bits` and writing those bits back produces the same bytes, with a padding count of 0. Padding is not a payload length. Callers that need to ignore trailing bits keep that count themselves.

## Huffman lengths

`dacoexec::huffman::count_frequencies` counts byte symbols in one input range. `dacoexec::huffman::accumulate_frequencies` adds the same counts into an existing table, so a histogram can grow without keeping the bytes.

```cpp
#include <dacoexec/dacoexec.hpp>

#include <string_view>

int main()
{
  std::string_view input = "AAAAABBCD";

  auto frequencies = dacoexec::huffman::count_frequencies(input);
  auto lengths = dacoexec::huffman::build_code_lengths(frequencies);

  return lengths[static_cast<unsigned char>('A')] == 1 ? 0 : 1;
}
```

The input is any `dacoexec::byte_input_range`: `std::byte`, `char`, `unsigned char`, `signed char`, or `char8_t`, including move-only, unsized, and single-pass ranges. Each element is converted to an unsigned byte before it is used as an index. Signed character values keep their low eight bits, so `0x00`, `0x80`, and `0xFF` stay distinct.

The table holds 256 `std::uint64_t` counters (2048 bytes). Counting walks the input once. A counter that would pass the maximum `std::uint64_t` throws `std::overflow_error` (`Huffman frequency counter overflow`). `count_frequencies` returns a finished table, or it throws and the caller receives nothing. `accumulate_frequencies` can leave earlier increments applied when a later byte throws. An exception from the source propagates unchanged.

`dacoexec::huffman::build_code_lengths` returns one `std::uint8_t` length per byte value. Zero means the symbol is absent. A present symbol receives a length of at least 1. An empty table is all zeros. One present symbol receives length 1. Two present symbols both receive length 1.

Ties break in a fixed order: lower frequency, then the smaller symbol contained in the subtree, then the earlier node. The same table yields the same lengths on every run. Construction uses a fixed array of 511 nodes and performs no dynamic allocation. A parent frequency that would pass the maximum `std::uint64_t` throws `std::overflow_error` (`Huffman tree frequency overflow`).

Lengths are the leaf depths of that tree and can be as large as 255. With two or more present symbols they form a complete prefix code: the Kraft sum is 1, and the weighted path length is optimal. Canonical codewords are assigned from these lengths later. This step produces lengths only.

```cpp
auto frequencies = dacoexec::huffman::count_frequencies(block.bytes_);
auto lengths = dacoexec::huffman::build_code_lengths(frequencies);
```

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
