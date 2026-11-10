# DAC1 arithmetic archives

DAC1 is the arithmetic codec family's archive format. The current internal
implementation supports static order-0 range coding and RAW frames through the
shared block, frame parser, and verified stream infrastructure. This milestone
covers internal archive infrastructure only. The public facade, umbrella header,
CLI, public views, and optional parallel integration are a separate milestone.

The owned block currently holds optional metadata specifically for static range
coding. Reserved variants are rejected before interpreting metadata; future
variants will introduce their own metadata representation as they are implemented.

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
| `02` | Adaptive order-0 range | Reserved |
| `03` | Binary arithmetic | Reserved |
| `04` | Context-adaptive binary arithmetic | Reserved |
| `ff` | END | Supported |

END has zero size and CRC fields and no body. Data frame sizes must be nonzero
and at most 16 MiB each. A configured frame limit further bounds both sizes.
Reserved variants report `unsupported_coder`; unknown tags report `invalid_frame`.

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
total is 4096. Each compressed frame carries its own complete model.

The payload follows the model and uses the byte-oriented range engine's
[initialization, carry handling, and finalization contract](range-coding.md).
It contains at least five bytes. Coding state and the model are independent
between frames. Exactly the declared number of bytes must decode, and the
payload must satisfy the range decoder's termination checks.

The encoder counts one histogram per block. The shared encoder chooses compressed
storage only when payload plus model metadata is strictly smaller than the
source; ties use RAW. Both encodings have the same frame header overhead.

The verified decoder checks output limits before decoding and CRC32 before
writing each frame to the destination. Earlier verified frames may have been
written if a later frame fails. Archive parsing requires END and rejects truncated
headers, models, payloads, nonzero END fields, and trailing bytes.
