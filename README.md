# dacoexec

Header-only C++20 Huffman and ANS archive compression. The core
`dacoexec::dacoexec` CMake target has no third-party dependencies.

```cmake
find_package(dacoexec CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE dacoexec::dacoexec)
```

## Supported API

Only declarations documented in these facade headers are supported:

| Header | Interface |
| --- | --- |
| `<dacoexec/dacoexec.hpp>` | All dependency-free facades below |
| `<dacoexec/huffman.hpp>` | Huffman archive compression and decompression |
| `<dacoexec/ans.hpp>` | ANS archive compression and decompression |
| `<dacoexec/views.hpp>` | Bit and integer adaptors |
| `<dacoexec/bit.hpp>` | MSB-first bit reader, writer, and output iterator |
| `<dacoexec/error.hpp>` | Archive, I/O, and codec exceptions |
| `<dacoexec/execution.hpp>` | Optional parallel compression with stdexec |

Everything under `dacoexec/detail/` is unsupported implementation, including
codec classes, trees, codebooks, frequency tables, probability models, block and
frame representations, serialization, integer primitives, node storage, and
generic sender machinery. These headers are installed so templates can compile;
installation does not make their declarations API. Old subsystem header paths
and low-level namespaces have been removed; there are no compatibility wrappers.

## Codecs

```cpp
#include <dacoexec/huffman.hpp>
#include <string>

std::string input = "hello hello hello";
auto compressed = dacoexec::huffman::compress(input);
auto restored = dacoexec::huffman::decompress(compressed); // vector<byte>
```

`ans::compress` and `ans::decompress` have the same interface. They read and
write complete archives, including empty archives, and estimate models
automatically. Custom ANS probability distributions are not currently public.
Huffman uses DHF1; ANS uses DAN1. Existing archive bytes and checksum validation
are preserved. Decompression requires the matching codec.

Inputs are viewable ranges of `std::byte`, `char`, `signed char`, `unsigned char`,
or `char8_t`, including unsized and single-pass ranges. As with standard C++20
adaptors, move a move-only view when transferring ownership. Convenience
operations return `std::vector<std::byte>` and materialize the complete result.

For incremental output, use `compress_stream(input, destination, options)` or
`decompress_stream(archive, destination, options)` in either codec namespace.
Destinations can be output iterators accepting `std::byte`, `std::ostream`
objects, or lvalue sink objects providing `void write(std::span<std::byte const>)`.

```cpp
#include <dacoexec/ans.hpp>
#include <iterator>
#include <vector>

std::vector<std::byte> archive;
auto encoded = dacoexec::ans::compress_stream(input, std::back_inserter(archive),
    { .block_size_ = 64 * 1024 });
std::vector<std::byte> output;
auto decoded = dacoexec::ans::decompress_stream(archive, std::back_inserter(output),
    { .max_frame_size_ = 16 * 1024 * 1024, .max_output_bytes_ = 1024 * 1024 });
```

Each codec has its own public `stream_options`, `decode_options`, and
`stream_statistics` value types. Options use the fields shown above. Block size
defaults to 1 MiB; block and frame limits must be between 1 and 16 MiB. Total
decoded output defaults to unrestricted; zero permits an empty archive only.
Statistics contain `input_bytes_`, `output_bytes_`, and `frames_` and are returned
only on success. Archive counts include framing; plain counts exclude it.
Streaming stages data per frame. Decoding verifies each frame before publishing
it. Source and destination exceptions propagate, and earlier accepted writes
remain after a later failure.

Catch `dacoexec::format_error` for framing, checksum, and output-limit failures,
`dacoexec::stream_error` for library-reported I/O failures, and
`dacoexec::huffman::decode_error` or `dacoexec::ans::decode_error` for malformed
codec payloads. Invalid options throw `std::invalid_argument`.

## Views and bit I/O

```cpp
#include <dacoexec/views.hpp>
#include <array>
#include <cstdint>

namespace dv = dacoexec::views;
std::array<std::uint64_t, 3> values{1, 2, 13};
auto decoded = values | dv::encode::gamma() | dv::decode::gamma(values.size());
```

Archive compression/decompression views are deferred until they can process
incrementally with bounded memory. Use codec stream functions for incremental
compression and decompression; the convenience functions materialize their result.

Integer adaptors include `encode::gamma()`, `encode::delta()`,
`encode::golomb(divisor)`, and `encode::rice(shift)`. Decoders take a count:
`decode::gamma(count)`, `decode::delta(count)`, `decode::golomb(divisor, count)`,
and `decode::rice(shift, count)`. Golomb/Rice decoders optionally accept
`views::decode_limits{ .max_unary_bits_ = limit }`; the limit excludes the
terminating zero. Gamma/Delta accept positive unsigned integers; Golomb/Rice
accept zero as well. Divisors must be positive and shifts less than 64.
Integer adaptors stage one integer at a time and yield bool bits or uint64 values;
iterator copies share a pass. They carry no archive framing or integer count.

`views::bits()` unpacks byte input as MSB-first bool bits. `<dacoexec/bit.hpp>`
provides `bit_reader`, `bit_sequence`, `bit_writer`, and `bit_output_iterator`.
The reader borrows a byte span with an explicit valid-bit count. The writer
accepts an output iterator; call `finish()` to flush it and obtain the padding
bit count. Retain that count when packing integer-encoded bits. Integer primitive
functions and numeric mapping helpers are private.

## Parallel compression

Enable `dacoexec_BUILD_EXECUTION`, request the installed `execution` component,
and link `dacoexec::execution` to use `<dacoexec/execution.hpp>`.

```cpp
#include <dacoexec/execution.hpp>
#include <stdexec/execution.hpp>

auto sender = dacoexec::huffman::compress_parallel_on(scheduler, input, destination,
    { .block_size_ = 64 * 1024, .max_in_flight_ = 4 });
auto result = stdexec::sync_wait(std::move(sender));
```

The ANS namespace offers the same operation. Each lazy, single-use sender
completes with the codec's public `stream_statistics`. Writes stay in source
order; outstanding work drains on failure or cancellation. Earlier writes remain.
Lvalue sources, sink/ostream objects, output storage, and scheduler contexts must
outlive completion. Output iterator adapters are owned by the sender. The generic
ordered sender is private. The core target and umbrella never require stdexec.

## CLI

```sh
dacoexec compress --algorithm huffman input.bin output.dhf
dacoexec compress --algorithm rans input.bin output.dan
dacoexec decompress output.dan restored.bin
```

The CLI detects the archive format for decompression. Enable `dacoexec_BUILD_CLI`
to build it; CLI11 is a private dependency.

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
| `dacoexec_BUILD_CLI` | OFF | OFF | Build the CLI11 compression/decompression executable |
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
