# Binary arithmetic engine

The internal `dacoexec::detail::arithmetic::binary_arithmetic_encoder` and
`binary_arithmetic_decoder` use a 32-bit inclusive interval with conventional
E1/E2/E3 normalization. They accept caller-supplied binary frequencies. Each
fresh object starts an independent stream. The decoder borrows its payload.

`encode(bit, probabilities)` and `decode(probabilities)` process one bit.
`encode_byte` and `decode_byte` process bits 7 through 0, in that order.
Models can change between calls if both sides supply identical frequencies;
the engine neither counts symbols nor updates probabilities.

## Fixed model

`binary_model` stores `zero_frequency_` and `one_frequency_`. Both must be
positive and their sum must not exceed 65536. Totals need not be powers of two.
`make_binary_model(input, precision)` counts all bits in the supplied byte
range, adds one pseudocount to each outcome, and uses the shared integer
normalizer. The default precision is 12 (total 4096); supported precisions
are 1 through 16. Empty input gives equal frequencies. Both outcomes retain
nonzero probabilities even for an all-zero or all-one source. Counting uses
checked 64-bit counters and accepts the usual byte input ranges.
Invalid precision is rejected before reading input, preserving single-pass sources.

Construct the fixed model once over the complete frame and use it for every
bit in that frame. Model serialization, archive tags, framing, and public codec
selection belong to the codec adapter described below.

## Adaptive context model

The internal `binary/context/model.hpp` provides `context_model`, a probability
policy for the same binary engine. Its 32 independent `binary_model` entries
start with counts `(1, 1)`. Before each bit, `probabilities()` selects
`(bit_position << 2) | previous_two_bits`, where position zero is the byte's
MSB. After encoding or decoding, call `update(bit)` exactly once. It increments
only the selected outcome in the selected context, then advances the two-bit
history and byte position. History starts at zero and persists across byte
boundaries; the position wraps after eight bits.

Use the engine's single-bit `encode` and `decode` methods with this policy,
selecting probabilities and updating the model after every bit. Do not pass
`model.probabilities()` to `encode_byte` or `decode_byte`: those fixed-model
helpers use the same probabilities for all eight bits and never update the
context policy.

Like the adaptive byte model, a context rescales only when its total exceeds
32768, replacing each count with `(count + 1) / 2`. This threshold is a fixed
binary-context convention independent of range-coder settings. Other contexts
remain unchanged. Both outcomes retain positive width. Construct a fresh model or call
`reset()` at every frame boundary to restore all counts, history, and position.
The engine's interval and finalization rules remain unchanged. This model is
an internal policy; archive selection and payload integration are separate.

## Interval and normalization

Initialize `low = 0`, `high = 0xFFFFFFFF`. Let `Q = 0x40000000`, `H = 2*Q`,
and `T` be the sum of the two frequencies. For each bit compute, in 64 bits:

```
width = uint64(high) - low + 1
split = low + (width * zero_frequency) / T
```

Zero selects `[low, split - 1]`; one selects `[split, high]`. Division rounds
down. Repeat the first applicable rule until no rule applies:

1. E1: if `high < H`, emit zero and all pending complementary bits.
2. E2: otherwise, if `low >= H`, emit one and all pending complementary bits,
   then subtract `H` from both endpoints.
3. E3: otherwise, if `low >= Q && high < 3*Q`, increment the pending count
   without emitting, then subtract `Q` from both endpoints.

After each applicable rule, set `low = low << 1` and
`high = (high << 1) | 1`, retaining 32 bits. The normalized interval has
width greater than `Q`; the frequency bound ensures neither subinterval is
empty. The largest multiplication is less than 2^48. All arithmetic is
unsigned and no floating point is used. Pending-bit count overflow is checked.

The decoder initializes a 32-bit code from the first 32 payload bits and
chooses one exactly when `code >= split`. It applies the same interval rules,
subtracting the corresponding offset from its code, then shifting in one
payload bit for each normalization. A missing bit is an error; there are no
implicit zeros beyond the payload.

## Finalization and validation

Output bits are packed MSB-first. After the final source bit:

1. Increment the pending count once.
2. Emit zero if `low < Q`, otherwise one, then emit the complement once for
   every pending bit.
3. Append exactly 30 zero guard bits.
4. Append zero padding to the next byte boundary.

If there were `R` normalization steps, the payload before byte padding has
exactly `R + 32` bits. This supplies every bit needed by decoder initialization
and its `R` shifts. Empty input produces `40 00 00 00`. Under a uniform model,
encoding the byte `69` produces `69 40 00 00 00`.

The source bit count is external; there is no end symbol. After decoding that
many bits, call `finish()`. The terminal code must equal `Q` when `low < Q`,
otherwise `H`. All that may remain is at most seven zero byte-padding bits.
This rejects truncated input, trailing bytes, and noncanonical termination;
it is not a checksum and cannot authenticate a supplied model or bit count.

A changed payload can be another canonically terminated stream representing
different output. A DAC1 adapter must preserve frame CRC32 verification and
decode exactly the externally declared number of bits before calling `finish()`.
Canonical termination and frame integrity are separate checks.

Both engines allow finalization exactly once. Subsequent operations report
`errc::invalid_state`. Invalid frequencies or precision report
`errc::invalid_argument`; malformed, truncated, or trailing payloads report
`errc::invalid_payload`. Discard an engine after an exception other than an
argument validation failure.

Golden payloads in `test/arithmetic_binary_tests.cpp` freeze interval rounding,
bit order, E1/E2/E3 handling, and finalization. Tests also exhaust short bit
sequences under several probability models, exercise long pending-bit runs,
and check changes of model between bits.

## DAC1 binary frames

Select `dacoexec::arithmetic::entropy_coder::binary` through `stream_options::coder_`
for owning, streaming, lazy view, or parallel compression. Decoding selects the
codec from each frame tag, allowing mixed static range, adaptive range, binary,
and RAW frames. The CLI automatically recognizes these archives on decompression.

Tag `0x03` uses the existing DAC1 frame header (tag, LE uint32 uncompressed byte
count, payload byte count, and CRC32). Immediately after the header are four
model bytes: LE uint16 `zero_frequency - 1`, then LE uint16 `one_frequency - 1`.
Their decoded sum must be at most 65536; larger sums are invalid frame metadata.
Model bytes are excluded from the payload byte count. The encoder uses the
smoothed default 12-bit model above; readers accept all positive frequencies
within the engine's total bound. These bytes precede the packed binary payload,
which must contain at least four bytes. No bit count or end symbol is stored.

The adapter decodes exactly eight MSB-first bits for each declared output byte,
then checks the engine's canonical termination and the frame CRC32 before
committing output. Model and interval state reset independently on each frame.
Compression falls back to RAW unless payload plus four model bytes is strictly
smaller than the source. Stream byte statistics include all model bytes.

Binary coding uses a two-symbol bit model and the E1/E2/E3 engine; static and
adaptive order-0 range coding use a 256-symbol byte model and a byte-oriented
range engine. Comparisons between them change both alphabet and engine.
