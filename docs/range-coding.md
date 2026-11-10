# Integer range engine

The internal `dacoexec::detail::arithmetic::range_encoder` and `range_decoder`
operate on caller-supplied cumulative intervals. They do not count frequencies,
choose symbols, serialize models, or frame archives. A fresh object starts an
independent stream. The decoder borrows its input span.

The internal archive adapter wraps static range blocks in [DAC1](dac1.md),
using shared RAW fallback, bounded framing, and verified stream output.

For each symbol, supply a total `T` in `[1, 2^24]`, a positive frequency `f`,
and a cumulative frequency `C` with `C + f <= T`. The caller's model must
partition `[0, T)` into nonoverlapping symbol intervals. Totals need not be
powers of two and may change between symbols, provided both sides agree.

The byte convention follows the carry-buffered 7z range coder described by
[Igor Pavlov's public-domain implementation](https://github.com/ip7z/7zip/blob/main/C/Ppmd7Enc.c),
with normalization immediately after every symbol. This is an integer range
engine, not a PPMd or LZMA codec.

* Initialize unsigned 64-bit `low = 0` and unsigned 32-bit `range = 0xFFFFFFFF`.
* Set `u = range / T`, `low += uint64(u) * C`, and `range = u * f`.
  The unused division remainder is discarded, including for the last symbol.
* While `range < 0x01000000`, shift `range` left eight bits and shift `low`
  using the carry procedure below. Equality does not trigger normalization.
* Start with cached byte zero and pending count one. To shift `low`, let
  `L = uint32(low)` and `carry = low >> 32`. If `L < 0xFF000000` or carry is
  nonzero, emit the cached byte plus carry modulo 256, followed by
  `pending - 1` copies of `0xFF + carry` modulo 256; cache `L >> 24` and clear
  pending. Increment pending, then set `low = uint32(L << 8)`.
* Finish with exactly five low shifts, without changing `range`. Empty input
  therefore produces five zero bytes. The first payload byte is always zero.

The decoder requires at least five bytes: a zero prefix followed by a four-byte
big-endian code, strictly smaller than `0xFFFFFFFF`. For each symbol, compute
`u = range / T` and `slot = code / u`; reject `slot >= T`. The caller finds the
symbol interval containing `slot`, then calls `decode(C, f, T)`. The decoder
sets `code -= u * C`, `range = u * f`, and while `range < 0x01000000` shifts
range by eight and sets `code = (code << 8) | next_byte`. Missing input is an
error; there is no implicit zero padding. `cumulative(T)` does not mutate state.

The symbol count is external: there is no end symbol. After exactly that many
symbols the caller must call `finish()`, which requires all input consumed and
code zero. These checks do not replace a checksum or validate the model/count;
some changes to data or symbol count can still form valid streams.

The total bound ensures `u >= 1` after normalization. Products fit in 32 bits;
low additions use 64 bits, with at most one carry bit. Low shifts explicitly
retain only the low 32 bits. No floating point is used. Encoding and
decoding after successful finalization, or finalizing twice, reports
`errc::invalid_state`. Invalid model intervals report `errc::invalid_argument`;
malformed, truncated, or trailing payloads report `errc::invalid_payload`.
Discard an engine after an exception other than an argument validation failure.

Golden vectors in `test/arithmetic_range_tests.cpp` freeze the byte convention,
including delayed carries, normalization boundaries, and finalization.
An independent base-256 arbitrary-width lower endpoint also checks every binary
word through length 12 under four explicit models, seeded changing intervals,
and deferred carry chains through 1024 bytes. This oracle uses ordinary wide
integer addition and final big-endian serialization, without the engine's
cached-byte emission procedure. Decoding those independently produced payloads
checks both sides against the oracle rather than relying solely on round trips.

Archive tests additionally freeze a multi-symbol DAC1 fixture (equal frequencies
for bytes 0 and 255), including normalized model bytes, renormalized payload,
CRC32, and END. Every single-bit change in that fixture's payload is rejected
before publishing the frame. Identical consecutive input blocks must reproduce
identical complete frames, verifying model and engine reset. Existing tests cover
malformed models, truncated and trailing data, mixed RAW/compressed frames,
output limits, public views, single-pass streams, and ordered parallel encoding
and decoding with bounded in-flight work.

The stage and complete-archive benchmark methodology and initial cross-codec
results are in [static range measurements](static-range-benchmarks.md).

## Static order-0 model

`detail/arithmetic/static_model.hpp` reuses the shared 256-symbol
`model::frequency_model` and its integer normalization. `make_static_model`
counts the input once, defaults to 12-bit precision (total 4096), and preserves
positive frequencies for every observed byte. Absent symbols stay at zero.
Empty input has no distribution and reports `invalid_argument`. Shared model
precisions from 1 through 16 bits are supported when the alphabet fits.

`static_range_codec.hpp` combines that immutable model with a fresh range
engine per block. `encode_block(input, model)` reads the input once in forward
order; counting and encoding a single-pass source require separate passes or
caller-owned buffering. Encoding a symbol absent from the model reports
`invalid_argument`. `decode_block(payload, model, count, limit)` checks the
required explicit output limit before allocation and validates engine termination. An empty block
can use an existing model and produces the engine's five zero bytes.

Models and payloads are separate. `serialize_model` writes one precision byte,
a little-endian uint16 nonzero-entry count, then entries in ascending symbol
order: one symbol byte and a little-endian uint16 frequency-minus-one. This
matches the existing ANS metadata representation, including a single frequency
of 65536 at 16-bit precision. `deserialize_model` accepts exactly one metadata
span, rejects unsorted or duplicate entries, invalid totals, truncation and
trailing bytes with `invalid_payload`. These helpers do not add archive framing.

Decoding uses binary search over cumulative frequencies, skipping zero-width
intervals. The optional `dacoexec_arithmetic_model_benchmarks` target compares
this with a 4096-entry byte lookup table and measures table construction
separately. It queries identical, uniformly distributed cumulative slots for
2-, 16-, and 256-symbol models, with uniform or skewed histograms.

`make_symbol_lookup(model)` builds the optional direct table used by the
benchmarks. It uses one byte per cumulative slot: 256 bytes at precision 8,
4 KiB at precision 12, and 64 KiB at precision 16. Tests compare its results
against binary search for every slot at every supported precision.

The `block_decode` benchmarks compare complete block decoding, including output
allocation, interval validation, normalization, termination checks, and, for the
direct variant, table allocation and construction on every block. Model counting,
serialization, and encoding happen outside the timed loop. Both variants are
checked against the original bytes before timing. The matrix covers 64-, 4096-,
and 65536-byte blocks, 2- and 256-symbol source alphabets, and precisions 8, 12,
and 16. Small blocks may observe fewer symbols than the source alphabet.

A preliminary run on an Intel Core i5-12450H, Clang 22.1.5, C++20, `-O2`, on
2026-11-10 measured these median full-block decoding rates at precision 12:

| Block bytes | Source alphabet | Cumulative search (MiB/s) | Direct table (MiB/s) |
| --- | --- | --- | --- |
| 64 | 2 | 27.5 | 50.5 |
| 64 | 256 | 27.1 | 35.0 |
| 4096 | 2 | 21.8 | 69.4 |
| 4096 | 256 | 27.0 | 61.4 |
| 65536 | 2 | 22.0 | 71.4 |
| 65536 | 256 | 27.0 | 66.1 |

Reproduce with:

```sh
dacoexec_arithmetic_model_benchmarks --benchmark_filter=block_decode \
  --benchmark_min_time=0.02s --benchmark_repetitions=3 \
  --benchmark_report_aggregates_only=true
```

CPU scaling was enabled and build work was active. These results support a
future direct-table decoding policy, but the production adapter retains binary
search for now. Precision and block size matter: a 64-byte block with a
256-symbol source alphabet at precision 16 measured 26.7 MiB/s for search and
13.3 MiB/s for direct lookup, including the 64 KiB table setup.

Inspection of Clang 22.1.5 `-O2` assembly for a wrapper calling `decode_block`
with runtime model precision showed separate calls to `cumulative` and `decode`.
They retain two `range / total` divisions plus one `code / unit` division per
symbol. Thus redundant unit calculation survives in that configuration. A
future prepared-step interface should preserve interval validation and reject
stale steps; this change keeps the existing range-engine contract intact.
