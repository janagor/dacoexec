# DHF1 version 1

DHF1 is a sequential, independently decoded block archive. All integers are
unsigned little-endian. The magic includes the version; incompatible versions
must use another magic. There is no archive length, index, or frame count.

The archive begins with exactly eight bytes: `44 48 46 31 00 00 00 00`.
The first four bytes are ASCII `DHF1`; the next two are flags and the last two
are reserved. Flags and reserved fields must be zero.

Every frame starts with a 13-byte header:

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 1 | Kind: RAW = 0, HUFFMAN = 1, END = 255 |
| 1 | 4 | Uncompressed byte count |
| 5 | 4 | Payload byte count |
| 9 | 4 | IEEE CRC32 of uncompressed bytes |

RAW stores exactly payload-count original bytes after its header. Both sizes
must be equal and positive. HUFFMAN stores 256 code-length bytes in symbol
order followed by exactly payload-count compressed bytes. The length table
is excluded from payload count. Zero lengths denote absent symbols; nonzero
lengths are at most 32. Codes are canonical, with equal lengths ordered by
symbol. The singleton alphabet uses a one-bit zero code; other nonempty
alphabets must form a complete prefix code. Bits are emitted most significant
first. After the declared number of symbols, fewer than eight zero padding
bits may remain. Extra whole bytes and incomplete codewords are invalid.

Both data-frame sizes must be in [1, 16777216]. A decoder may enforce a lower
limit before allocating payload storage. Unknown kinds are invalid. END has
zero sizes and CRC, no payload and no length table. END is mandatory and no
bytes may follow it. Empty input encodes as the archive header followed by END.
Truncation at any point, including missing END, is an error.

CRC32 uses the reflected IEEE polynomial `0xEDB88320`, initial state
`0xFFFFFFFF` and final XOR `0xFFFFFFFF`. The check value for ASCII `123456789`
is `0xCBF43926`. CRC32 detects corruption; it is not authentication.

The default encoder block size is 1048576 bytes and its maximum is 16777216.
Huffman is selected only when payload count plus 256 is strictly less than
original count; otherwise RAW is selected. Identical input and block settings
produce identical output. Total archive and output sizes may exceed 4 GiB.

# Library usage

Include `dacoexec/dacoexec.hpp`. Pass a byte input range and an object providing
`write(std::span<const std::byte>) -> void` to `huffman::compress_stream` or
`huffman::decompress_stream`. `io::ostream_sink` and `io::iterator_sink` adapt
streams and iterators. Source ranges may be single-pass, non-sized and
move-only. A source adapter must report read errors rather than treating them
as EOF. Exceptions propagate unchanged.

`views::frames(limit)` is lazy until begin, consumes and validates the archive
header, and stages only one owning `encoded_frame`. Increment releases that
frame before parsing another. Dereference transfers ownership; repeated
dereference observes a moved-from value. Iterators share the pass and remain
valid when the view is moved. Any parse failure ends the pass permanently.
The parser validates canonical lengths but leaves payload decoding to the codec.
Parsed DATA frames receive consecutive `block_.sequence_` values starting at
zero, independent of their RAW or HUFFMAN encoding. END has no sequence number.
After sequence 2^64-1, another DATA frame throws `std::overflow_error`; END is
still accepted. Moving the view preserves this counter along with the cursor.

`encode_frame` and `write_frame` expose computation and serialization separately
for future scheduling. Processing is sequential and retains O(B) working
memory, independent of input length. Statistics count successful archive bytes,
uncompressed bytes and DATA frames using checked 64-bit arithmetic; END does
not count as a DATA frame. Statistics are returned only on complete success.
Decompression checks output limits and validates decoding and CRC before
writing any bytes of the current frame. Earlier frames may already have been
written when a later frame fails. Sink failures may leave a partial write;
the library does not retry or roll back.

# CLI

Build with `-Ddacoexec_BUILD_CLI=ON` to enable CLI11 2.6.1. Core-only consumers
do not resolve or link CLI11.

```
dacoexec compress --algorithm huffman --block-size 1048576 input.bin output.dhf
dacoexec decompress --max-output-size 1073741824 output.dhf restored.bin
```

Existing output requires `--force`; identical input/output files are rejected.
The CLI holds an exclusively created same-directory temporary file handle
through all writes, flushes and closes it successfully, then
publishes it. Failures remove temporary files. Without force, publication uses
an exclusive hard link to prevent replacing a concurrently created destination;
the filesystem must support hard links. Forced publication uses rename (MoveFileExW with replacement on Windows). This is not a crash-durability guarantee.
Errors go to stderr and return a nonzero status.

The destination directory must be trusted: other processes must not be able
to replace the temporary directory entry during processing or publication.
Writes retain the exclusive handle, but publication and cleanup use its
pathname. Hostile replacement of that entry may cause another file or symlink
to be published. The CLI does not provide protection against hostile directory
modification; the no-force destination-exists protection still applies.

# Validation

The regular Catch2 suite includes golden bytes, malformed archives, codec and
stream failure tests, plus deterministic destination-publication races. Each
public header also compiles in its own strict C++20 translation unit, without
unity builds or precompiled headers. `streaming.memory` measures retained ordinary and aligned heap allocation
for increasing input lengths in a separate process. Configure
`-Ddacoexec_TEST_LARGE_STREAMS=ON` to register `streaming.large`, which processes
4 GiB + 4096 bytes in both directions using generated sources and checking
sinks. It retains neither the source nor the archive and is excluded by default.
