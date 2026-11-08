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

## Integer coding

The dependency-free `integer_coding/gamma.hpp`, `integer_coding/delta.hpp`, `integer_coding/golomb.hpp`, and `integer_coding/rice.hpp` headers expose universal and parameterized integer codes in `dacoexec::integer_coding`. They are also included by the umbrella header. Gamma and Delta accept positive `std::uint64_t` values; Golomb and Rice accept the full nonnegative domain. Golomb requires a positive `std::uint32_t` divisor, and Rice requires a shift below 64. Invalid arguments throw `std::invalid_argument` before emitting bits.

```cpp
std::vector<std::byte> encoded;
dacoexec::bit_writer writer{ std::back_inserter(encoded) };
constexpr auto sample_value = std::uint64_t{ 13 };
dacoexec::integer_coding::write_gamma(writer, sample_value);
auto const padding = writer.finish();
constexpr auto bits_per_byte = 8U;
auto const valid_bits = encoded.size() * bits_per_byte - padding;
dacoexec::bit_reader reader{ std::span<std::byte const>{ encoded }, valid_bits };
auto const decoded = dacoexec::integer_coding::read_gamma(reader);
```

`bit_reader` borrows its storage and reads MSB first. Its constructor requires an explicit valid bit count and rejects limits beyond the supplied span. `read_bits(n)` returns a `std::uint64_t` for widths zero through 64. Exhausted reads throw `std::out_of_range`; an invalid width throws `std::invalid_argument`. Failed `read_bits` calls leave the cursor unchanged. `remaining_bits()` reports the unconsumed valid bits. Padding and trailing bytes are excluded by the declared limit; validating their contents is the caller's responsibility.

Golomb and Rice encode the quotient as ones followed by a terminating zero. Primitive decoding is unrestricted by default, matching the encoders’ full integer domain. For untrusted input, pass an explicit `integer_coding::decode_limits{ .max_unary_bits_ = limit }` to either reader to bound work per integer. The limit excludes the terminating zero and is inclusive; zero permits only quotient zero. Exceeding that limit throws `std::length_error`; integers outside the 64-bit domain throw `std::overflow_error`. Decoding failures can consume a prefix of the input. Small divisors or Rice shifts can produce very large outputs, so callers should choose parameters appropriate to their data.

`gamma_bit_length`, `delta_bit_length`, `rice_bit_length`, and `golomb_bit_length` report codeword sizes without allocations. Rice and Golomb size helpers throw `std::overflow_error` if the mathematical bit count exceeds `UINT64_MAX`, and reject invalid parameters. Callers can compare the size with their output budget before writing. Encoders do not impose that budget themselves. The integer coding primitives and reader support constant evaluation with a suitable writer; the existing `bit_writer` remains a runtime writer. These primitives carry no integer count or framing. Callers must retain decoding parameters, the valid bit count, and the number of integers. DHF1 and the CLI are unchanged.

`integer_coding/sequence.hpp` adds `write_gamma_sequence(writer, input)` and `read_gamma_sequence(reader, count, output_iterator)`, with corresponding Delta, Golomb, and Rice functions. Golomb and Rice take their parameter after the input range or output iterator; readers also accept optional decoding limits. Input ranges contain unsigned integers (excluding bool), are consumed once, and may be move-only or single-pass. Writers do not finalize the bitstream. Readers decode exactly the requested count, return the advanced output iterator, and leave subsequent bits available. Output storage must have sufficient capacity and preserve `uint64_t` values. Exceptions propagate without rolling back earlier input or output.

`views/integer_encode.hpp` and `views/integer_decode.hpp` provide lazy, single-pass bit ranges, also available through the umbrella header:

```cpp
namespace dv = dacoexec::views;
std::vector<std::uint64_t> values{1, 2, 3, 4, 13};
auto decoded = values | dv::encode::gamma() | dv::decode::gamma(values.size());
```

The same interface supports `encode::delta()` / `decode::delta(count)`, `encode::golomb(divisor)` / `decode::golomb(divisor, count)`, and `encode::rice(shift)` / `decode::rice(shift, count)`. Golomb and Rice decoders accept an optional final `integer_coding::decode_limits` argument. Encoder inputs use the same unsigned integer constraint as the sequence API; decoder inputs contain `bool` values, including proxy references such as `vector<bool>`. Use `views::bits()` to unpack bytes before decoding, and `bit_output_iterator` with a `bit_writer` to pack encoder bits. Preserve the count and valid bit length alongside packed data.

Construction consumes no input. Each encoder stages one integer using fixed storage, including for very long unary prefixes. Decoders stage one integer at a time, stop at the explicit count, and leave trailing bits unread; a zero count never starts the source. Truncation throws `std::out_of_range`, while overflow and unary limits follow the primitive decoder exceptions. Any source or codec exception permanently ends the view. Iterator copies share a cursor, repeated `begin()` resumes it, and iterators survive moves of the owning view. Default and moved-from views are empty. Borrowed source storage must outlive iteration. For bulk compression, the existing writer-based APIs avoid per-bit iterator overhead.

For custom compositions, `write_sequence(writer, input, encoder)` calls `encoder(writer, value)` for each integer, and `read_sequence(reader, count, output, decoder)` calls `decoder(reader)` for each result. Numeric transforms remain in `dacoexec::numeric`: `numeric/mapping.hpp` provides `zigzag_encode(int64_t)` and `zigzag_decode(uint64_t)` separately from coding. To use Gamma or Delta, map the unsigned result with `numeric::to_positive` and undo it with `numeric::from_positive`; `to_positive(UINT64_MAX)` throws rather than overflowing. Consequently, this composition excludes `INT64_MIN`, whose ZigZag value is `UINT64_MAX`. Golomb and Rice accept that ZigZag value directly.

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

`dacoexec::bit_writer` packs those bits into any output iterator of `std::byte`. `write_bit` writes one bit. `write_bits({ .value_ = value, .bit_count_ = n })` writes the least significant `n` bits of `value`, most significant of those bits first. `n` is 0 through 32. A zero-length write of value 0 does nothing. A count above 32, or a value with bits set above that width, throws `std::invalid_argument`. A finalized or failed writer throws `std::logic_error` before those argument checks.

The writer keeps a single partial byte. Filled bits sit in the high positions, and a full byte is emitted immediately. `finish()` writes a trailing partial byte padded with zero bits on the right and returns how many padding bits were added (0 through 7). Calling `finish()` again returns the same count and writes nothing. The destructor does not pad or flush. Writing after `finish()`, or after the output iterator throws, throws `std::logic_error`. An output failure propagates as the original exception. Bytes the sink already accepted stay written, and the failed byte is not retried.

Moving a writer transfers its partial byte and its output iterator. Move assignment discards the destination writer's previous partial byte without writing it. If moving the output iterator throws, both writers become unusable and neither emits its partial byte.

```cpp
std::vector<std::byte> encoded;
dacoexec::bit_writer writer{std::back_inserter(encoded)};

writer.write_bits({ .value_ = 0b101u, .bit_count_ = 3 });
writer.write_bits({ .value_ = 0b11001u, .bit_count_ = 5 });
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

Tree nodes use pointer links. The default overload owns a `static_tree_arena`;
you can also pass typed storage explicitly:

```cpp
dacoexec::huffman::static_tree_arena arena;
auto lengths = dacoexec::huffman::build_code_lengths(frequencies, arena);

dacoexec::huffman::dynamic_tree_allocator<> dynamic;
auto dynamic_lengths = dacoexec::huffman::build_code_lengths(frequencies, dynamic);
```

Both strategies support C++20 constant evaluation when their owners are destroyed
within that evaluation. `static_node_arena<T, Capacity>` default-constructs all
slots and throws `std::length_error` when exhausted. `dynamic_node_allocator<T,
Allocator>` allocates each node separately and releases all nodes on destruction,
including after a failed build. Its pointer bookkeeping uses `std::vector` with
the default allocator. Both owners disable copying and moving to keep node
addresses stable. Calls append nodes to the supplied storage; use a fresh owner
for each tree when reusing a fixed-capacity arena.

For runtime PMR allocation, keep the resource alive until the storage is destroyed:

```cpp
#include <memory_resource>

using node = dacoexec::huffman::static_tree_arena::value_type;
using allocator = std::pmr::polymorphic_allocator<node>;
std::pmr::monotonic_buffer_resource resource;
dacoexec::huffman::dynamic_tree_allocator<allocator> storage{ allocator{ &resource } };
auto lengths = dacoexec::huffman::build_code_lengths(frequencies, storage);
```

PMR-backed storage is runtime-only. Custom storage must provide `create(node)`
returning a pointer whose address remains stable throughout construction.

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

## Canonical Huffman codewords

Canonical codebooks are immutable values reconstructed from code lengths alone.
The lazy view owns its codebook and stages one codeword at a time:

```cpp
#include <dacoexec/dacoexec.hpp>

#include <iterator>
#include <vector>

auto frequencies = dacoexec::huffman::count_frequencies(block.bytes_);
auto lengths = dacoexec::huffman::build_code_lengths(frequencies);
auto codebook = dacoexec::huffman::make_canonical_codebook(lengths);
auto encoded_symbols = block.bytes_ | dacoexec::huffman::views::codewords(codebook);

std::vector<std::byte> output;
dacoexec::bit_writer writer{ std::back_inserter(output) };
for (auto word : encoded_symbols) {
  writer.write_bits({ .value_ = word.bits_, .bit_count_ = word.length_ });
}
auto padding = writer.finish();
```

`"AAAAABBCD"` produces `0x05 0x6E` with one zero padding bit.
Empty codebooks and length-one singletons are accepted; multiple symbols must
form a complete prefix tree. Invalid tables throw `std::invalid_argument`.
Lengths above 32 throw `std::length_error`; `try_make_canonical_codebook(lengths)`
returns `std::nullopt` for these unsupported widths, preparing for a future RAW
block fallback without changing the original Huffman lengths. Width detection
precedes structural validation when a table contains an unsupported length.

The view accepts the library's byte element types, including proxy references
and move-only sources. Construction reads nothing; `begin()` stages the first
symbol, and each increment advances one symbol. Repeated dereferences use the
cached codeword. Iterator copies share a single pass, and moving the view keeps
active iterators valid. The owning view must outlive its iterators. Source
exceptions propagate unchanged; an absent symbol throws `std::invalid_argument`.
Either error permanently ends that view's iteration.

### Owned Huffman blocks and lazy decoding

`<dacoexec/huffman/block.hpp>` provides move-only `encoded_block` and consumes
owned blocks by value. Sequence numbers survive compression and decompression:

```cpp
constexpr std::uint64_t sequence = 42;
constexpr std::size_t block_size = 4096;
auto input = dacoexec::byte_block{
    sequence, std::vector<std::byte>(block_size, std::byte{'A'})};
auto encoded = dacoexec::huffman::compress_block(std::move(input));
auto decoded = dacoexec::huffman::decompress_block(std::move(encoded), block_size);
```

Compression selects canonical Huffman only when `ceil(encoded_bits / 8) + 256`
is strictly smaller than the input. The 256-byte cost is the complete code length
table; ties, empty input and unsupported code lengths above 32 bits select RAW.
RAW moves the original buffer, has zero lengths and padding, and its payload size
must equal the declared uncompressed size. Huffman blocks represent nonempty
input and contain MSB-first packed codewords with 0–7 zero-filled low padding
bits. This is an internal block model, not an on-disk format.

`decompress_block` checks the explicit output limit before allocating output.
Malformed metadata, invalid canonical lengths, truncated codewords, invalid
transitions, nonzero padding, extra payload and mismatched output counts throw
`dacoexec::huffman::decode_error`. Allocation failures propagate normally; no
partially decoded result is returned.

For incremental consumption, include `<dacoexec/huffman/decode_view.hpp>`:

```cpp
auto decoded = encoded_bytes | dacoexec::huffman::views::decode(
    codebook, valid_bit_count, expected_symbol_count);
for (std::byte symbol : decoded) {
    consume(symbol);
}
```

The view copies the codebook, owns rvalue sources and borrows suitable lvalues.
Construction reads nothing; `begin()` stages the first byte. Iterator copies
share one position, and moving the view preserves active iterators. The view is
move-only, single-pass and non-borrowed; destroying its owner invalidates its
iterators. Any decoding or source failure permanently ends iteration, while
source exceptions propagate unchanged. Exhaust iteration to validate the exact
bit and symbol counts, padding and absence of trailing bytes.

The lazy decoder holds a fixed-capacity trie, one cached source byte and one
staged output byte in a single owned state allocation; it does not allocate an
output-sized buffer or materialize bits. The eager codec allocates at most
block-sized buffers and does not buffer an entire stream. Both headers are
available through the dependency-free `dacoexec::dacoexec` interface target and
the umbrella header.

### DHF1 streaming and CLI

`<dacoexec/huffman/stream.hpp>` exposes sequential `compress_stream` and
`decompress_stream` for byte ranges and bulk output sinks. The default block
size is 1 MiB, with a 16 MiB hard limit. Archives include CRC32 integrity checks,
independent RAW or Huffman frames, and a mandatory terminator. See the
[normative DHF1 specification](docs/dhf1.md) for layout, error semantics and limits.

```cpp
std::vector<std::byte> archive;
dacoexec::io::iterator_sink sink(std::back_inserter(archive));
auto statistics = dacoexec::huffman::compress_stream(input, sink);
```

Enable `dacoexec_BUILD_CLI` to build and install the `dacoexec` executable:

```sh
dacoexec compress --algorithm huffman input.bin output.dhf
dacoexec decompress output.dhf restored.bin
```

Existing destinations require `--force`. Failed operations preserve existing
outputs. CLI11 2.6.1 is resolved only for CLI builds; the header-only core has
no additional dependency. Dedicated large-stream coverage is enabled with
`dacoexec_TEST_LARGE_STREAMS`; it is excluded from default test runs.

### Optional parallel Huffman encoding

Enable `dacoexec_BUILD_EXECUTION=ON` to provide `dacoexec::execution`.
It resolves an existing `STDEXEC::stdexec` target, an installed `stdexec`
package, or NVIDIA/stdexec v0.11.0 pinned to
`ead186b1d8db3ebe37a946ff84a6ce08bf795153`. The option defaults to OFF;
core-only builds and `<dacoexec/dacoexec.hpp>` do not require stdexec.

```cpp
#include <dacoexec/huffman/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

constexpr std::size_t workers = 4;
exec::static_thread_pool pool{ workers };
auto sender = dacoexec::huffman::encode_parallel_on(
  pool.get_scheduler(), input, sink);
auto result = stdexec::sync_wait(std::move(sender));
```

The single-use sender is lazy and completes with `stream_statistics`,
`std::exception_ptr`, or stopped. It owns rvalue ranges and borrows lvalue
ranges. Keep the sink, borrowed input, and scheduler context alive until
completion. Source reads and sink writes are serialized; they may migrate
from the starting thread to the worker that completes a batch. Thread-affine
sources and sinks need an external adapter.

`parallel_options` defaults to 1 MiB blocks and four blocks per batch.
Only independent block encoding runs concurrently. Every batch drains before
its frames are written in input order, producing identical DHF1 bytes to
`compress_stream()`. No next-batch block is acquired until the current
batch is committed and released. Retained input and result storage is
`O(max_in_flight * (block_size + encoded_frame_size))`, independent of input
length. Codec temporaries add per-worker storage, and input adaptors may
have their own staging buffers; 4 MiB is input payload capacity, not total
process memory.

Cancellation comes from the receiver environment. An already stopped
operation reads and writes nothing. Cancellation during a batch requests
stop on scheduled work and drains running compression before completing
stopped. Running CPU compression is cooperative at block boundaries.
Worker errors are collected before committing any frame from their batch;
the lowest input index with an error wins, including when cancellation races
with a recorded error. Internal errors stop further dispatch but do not cancel
already-started siblings: all are drained to preserve deterministic error
precedence. External cancellation still propagates to scheduled children through
the receiver environment. Earlier batches remain written.
Source, scheduler, and sink exceptions propagate without retries. A sink
failure can leave a partially written frame; neither failure nor observed
cancellation writes END.

Installed consumers request the optional component:

```cmake
find_package(dacoexec CONFIG REQUIRED COMPONENTS execution)
target_link_libraries(app PRIVATE dacoexec::execution)
```

This requires an installed stdexec package discoverable through
`CMAKE_PREFIX_PATH` or `stdexec_DIR`. Installed consumers never download it.
A plain `find_package(dacoexec CONFIG REQUIRED)` loads only the core target.

Optional [Huffman fuzzing](fuzz_test/README.md) and
[performance and memory measurements](benchmarks/README.md) validate the codec
through its public APIs. Development presets keep their dependencies private;
see [building](README_building.md) for setup.

### ANS entropy coding

`dacoexec::ans` provides immutable normalized integer models, binary-alphabet
rANS, and general byte-alphabet rANS. Binary coding is a two-symbol specialization
of the same arithmetic engine. Distinct ABS formulas and tANS are deferred.

```cpp
namespace ans = dacoexec::ans;
auto model = ans::make_binary_model(6, 2);
std::vector<bool> bits{ false, false, true, false };
auto packed = ans::binary::encode_block(bits, model);
auto decoded = ans::binary::decode_block(packed, model, bits.size());
```

For byte ranges, use `ans::count_frequencies`, `ans::make_model`, and
`ans::rans::encode_block` / `decode_block`. Empty input can be encoded with an
existing model; building a model from all-zero counts throws. Precision is
configurable from 1 through 16 bits (default 12), with at least one slot per
observed symbol. Normalization reserves those slots then distributes remaining
slots proportionally using integer arithmetic and largest remainders, breaking
ties by symbol index. Counts and their sum are checked for overflow.

The byte codec traverses bidirectional input backward directly, and buffers
single-pass or forward-only input for reverse traversal. Its 32-bit state starts at
`1 << 23`, emits low bytes during renormalization, and serializes the final
state as four little-endian bytes followed by emitted bytes in reverse order.
Decoding requires an expected symbol count and optionally an output limit;
it rejects truncated input, invalid states, trailing bytes, and a terminal
state different from the initial encoder state. A correct external model and
symbol count are required; bare payloads do not include integrity protection.

Block pipelines make ownership and buffering explicit:

```cpp
auto model = ans::make_model(ans::count_frequencies(data));
auto encoded_blocks = data | dacoexec::views::blocks(ans::default_block_size)
  | dacoexec::views::encode::rans(model);
auto decoded_blocks = std::move(encoded_blocks)
  | dacoexec::views::decode::rans(ans::default_block_size);
```

These single-pass adaptors stage one owned result on `begin()` and each
increment. Dereference returns a stable `const &` until increment or explicit
ownership transfer; repeated
reads do not recompute coding or consume the staged result. Use
`for (auto const &block : decoded_blocks)` to borrow decoded blocks, or
`std::ranges::iter_move(cursor)` to explicitly transfer ownership. Iterator
copies share the pass, and moving the view preserves existing cursors. Source
or codec exceptions permanently end the pass.

`ans::compress_block` builds a per-block model and falls back to RAW unless
payload plus model is strictly smaller. Sparse model metadata occupies three
header bytes plus three bytes per present symbol: six bytes for a single-symbol
model and 771 for all 256 symbols. `ans::decompress_block` requires an output limit.

`views::ans_frames()` follows the same borrowed-dereference contract: read a
frame with `auto const &frame = *cursor`, and transfer ownership with
`std::ranges::iter_move(cursor)`. Increment invalidates the borrowed frame.

`ans::compress_stream` and `ans::decompress_stream` use a separate DAN1 format.
DHF1 is unchanged. DAN1 starts with `DAN1` and four zero bytes. Each frame has a
one-byte tag and three little-endian uint32 fields: decoded size, payload size,
and uncompressed IEEE CRC32. RAW (0) contains bytes; rANS (1) contains one
precision byte, a little-endian uint16 entry count, and entries in strictly
increasing symbol order. Each entry contains one symbol byte and a little-endian
uint16 storing its positive frequency minus one (representing 1 through 65,536).
Absent symbols have frequency zero. Entry counts outside 1 through 256,
duplicate/unordered symbols, and invalid frequency sums are rejected. Payload
bytes follow the model, which is excluded from payload size. END (255) has zero
fields and must end the input.
Data sizes are positive and limited to 16 MiB. Decompression validates a complete
frame and its checksum before writing it; previously written frames remain if a
later frame fails. Per-frame and total output limits are configurable.

```sh
dacoexec compress --algorithm rans input.bin output.dan
dacoexec decompress output.dan restored.bin
```

Include `<dacoexec/ans/execution.hpp>` for optional stdexec-backed
`ans::encode_parallel_on`; it uses bounded batches and writes frames in source
order, producing the same bytes as sequential compression.
