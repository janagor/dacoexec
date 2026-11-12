# DAC1 arithmetic archives

DAC1 supports static and adaptive order-0 range coding, fixed and context-adaptive
binary arithmetic coding, and RAW frames through shared block, frame parser,
and verified stream infrastructure. The public facade, views, and optional
parallel streams accept all four coders.
The CLI's `arithmetic` selector uses static range coding; decompression reads
each coder from its frame tag.

Static range and binary blocks each carry a fixed frequency model. Adaptive
range and context binary blocks carry no model metadata.

An archive starts with `44 41 43 31 00 00 00 00` (`DAC1` plus four zero bytes),
contains zero or more data frames, and ends with one END frame. No bytes may
follow END. The empty archive is 21 bytes.

Every frame starts with this 13-byte header. All multibyte integers are unsigned
and little-endian.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | Frame tag |
| 1 | 4 | Uncompressed byte count |
| 5 | 4 | Payload byte count, excluding model metadata |
| 9 | 4 | IEEE CRC32 of uncompressed bytes |

| Tag | Meaning | Availability |
| --- | --- | --- |
| `00` | RAW | Supported |
| `01` | Static order-0 range | Supported |
| `02` | Adaptive order-0 range | Supported |
| `03` | Binary arithmetic | Supported |
| `04` | Context-adaptive binary arithmetic | Supported |
| `ff` | END | Supported |

END has zero size and CRC fields and no body. Data frame sizes must be nonzero
and at most 16 MiB each. A configured frame limit further bounds both sizes.
Unknown tags report `invalid_frame`; unknown public coder selectors report
`unsupported_coder`.

RAW stores the source bytes immediately after its header, without model
metadata. Its payload and uncompressed counts must match.

Static range stores a model immediately after its header:

- One byte: frequency precision (the encoder uses 12).
- Two bytes: number of nonzero frequency entries, from 1 through 256.
- For each entry: one symbol byte and two bytes containing its frequency minus one.

Entries must be strictly increasing by symbol. Absent symbols have zero
frequency. Frequencies must sum to `1 << precision`. Metadata uses the existing
[static model representation](range-coding.md); the decoder accepts the model's
supported precisions from 1 through 16. At the default precision the normalized
total is 4096. Each static compressed frame carries its own complete model.

Adaptive range stores its payload immediately after the header, with no model
metadata. All 256 frequencies start at one. After coding each byte, increment
its frequency; when total exceeds 32768, replace each frequency with
`(frequency + 1) / 2`. Reset the model at every frame boundary. This rule and
threshold are part of the DAC1 tag `02` format contract.

Binary arithmetic stores four model bytes immediately after its header: two
LE uint16 frequency-minus-one values, zero followed by one. Both decoded
frequencies must be positive and their sum must not exceed 65536. The encoder
counts all source bits once, adds one pseudocount per outcome, and normalizes
to total 4096 using the shared model normalizer. Metadata is excluded from the
payload size. Readers accept any model satisfying the total bound.

The binary payload follows these model bytes and uses the
[binary engine's MSB-first packing and canonical termination](binary-arithmetic-coding.md).
It contains at least four bytes. The decoder reconstructs exactly eight bits
per declared output byte, then verifies termination. Model and coding state
reset at each frame boundary. Public compression selects this format with
`entropy_coder::binary`.

Context-adaptive binary stores its payload immediately after the header, without
model metadata. Select it with `entropy_coder::context_binary`. Its 32 binary
models start with frequencies `(1, 1)`. Bits are processed MSB-first, using
`context = (bit_position << 2) | previous_two_bits`, with position zero at the
MSB. History starts at zero and persists across byte boundaries. After each
bit, increment the selected outcome's frequency; if that context's total exceeds
32768, replace both frequencies with `(frequency + 1) / 2`. Then advance history
and bit position. Every frame resets all probabilities, history, and coding state.
The payload uses the same binary engine termination contract and four-byte
minimum as fixed binary coding. These rules are frozen by tag `04`; a future
bitstream-changing context scheme requires a distinct format identifier.

The static payload follows the model and uses the byte-oriented range engine's
[initialization, carry handling, and finalization contract](range-coding.md).
It contains at least five bytes. Coding state and the model are independent
between frames. Exactly the declared number of bytes must decode, and the
payload must satisfy the range decoder's termination checks.

The static encoder counts one histogram per block. The adaptive encoder updates
its model while coding and counts no histogram. The shared encoder chooses compressed
storage only when payload plus model metadata is strictly smaller than the
source; ties use RAW. All encodings have the same frame header overhead.

The verified decoder checks output limits before decoding and CRC32 before
writing each frame to the destination. Earlier verified frames may have been
written if a later frame fails. Archive parsing requires END and rejects truncated
headers, models, payloads, nonzero END fields, and trailing bytes.
