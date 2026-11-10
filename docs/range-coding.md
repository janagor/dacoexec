# Integer range engine

The internal `dacoexec::detail::arithmetic::range_encoder` and `range_decoder`
operate on caller-supplied cumulative intervals. They do not count frequencies,
choose symbols, serialize models, or frame archives. A fresh object starts an
independent stream. The decoder borrows its input span.

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
