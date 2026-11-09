# tANS

Select `dacoexec::ans::entropy_coder::tans` in `stream_options::coder_`
(or `parallel_options::coder_` with the optional execution target). The CLI
accepts `compress --algorithm tans --table-log 10`. Decompression detects each frame's coder.
rANS remains the default and its encoding is unchanged.

`tANS` accepts table logs 8 through 12, with default 10. The setting is ignored
for rANS in the C++ API. The CLI rejects an explicit `--table-log` unless
`--algorithm tans` is selected. Invalid tANS table logs are rejected before writing the archive header.
Unsupported `entropy_coder` values also throw `std::invalid_argument` before writing.
RAW fallback includes model and payload costs.

DAN1 tag 2 extends the existing format; older readers reject this tag. Tags 0,
1 and 255 retain their meaning. A tANS frame uses the same normalized model
metadata as rANS, followed by its own payload. No tables are serialized.

The table uses the FSE spread step `(M >> 1) + (M >> 3) + 3`, wrapping modulo M,
placing symbols in increasing byte order. For each slot in state order, take
n from the symbol's frequency and increment it. Its bit width is
`table_log - floor(log2(n))`; its baseline is `(n << bit_width) - M`.
See [RFC 8878 section 4.1](https://datatracker.ietf.org/doc/html/rfc8878#section-4.1)
for the FSE transition background. This codec does not use the Zstandard wire format.

Payload layout is two little-endian uint32 fields (decoder state and exact bit
count), then MSB-first packed bits. Encoding starts at state zero and processes
symbols in reverse order. Each transition emits low bits least significant first
into a temporary packed stack. The entire bit sequence is reversed for storage.
Unused low bits of the last byte are zero. Decoding must consume all valid bits
and end at state zero after exactly the declared symbol count. Empty standalone
blocks contain state zero, bit count zero, and no bit bytes. Empty archives have
no data frames.

The bitstream implementation packs transition groups through an accumulator,
then reverses bytes with a shift for the final partial byte. Decoding reads groups
through an accumulator. The wire format is identical to the initial bitwise
implementation; tests compare against a slow specification packer and fixed vectors.
Decoder entries occupy four bytes and encoder slots two bytes, for 24 KiB of
transition tables at log 12 (excluding the model and vector metadata).

The encoder precomputes an eight-byte transform for each symbol (2 KiB total).
Its `delta_nb_bits` bias selects the emitted bit count with one fixed shift;
`delta_find_state` maps the reduced state into the shared encoding table.
The per-symbol transition uses shifts, additions and a table lookup, with no
variable-length bit-count loop. These transforms preserve the existing spread
and wire format. The 24 KiB figure above excludes these per-symbol transforms.

`dacoexec_ans_benchmarks` separates frequency counting, normalization, table
construction, symbol encoding/decoding, and complete archive encoding/decoding.
Model preparation is outside the table-construction timing loop. Symbol benchmarks
reuse prebuilt tables but include output allocation and bit packing. Archive
benchmarks include model preparation, framing, CRC32 and RAW fallback. Results
report five repetitions and aggregate statistics, including medians. Synthetic
alphabets exercise different entropy levels; they do not replace a real-dataset
comparison or establish an advantage over rANS or Huffman.

The decoder maintains `bytes_read * 8 == position + buffered` at transition
boundaries. Validated payload length and transition bit demand imply that a
refill byte exists; debug builds also assert this immediately before reading it.

Table construction benchmarks vary table log (8–12), alphabet size (1, 2, 17,
256), and uniform versus skewed counts. Both archive directions include Huffman,
rANS and tANS (coder arguments 2, 0 and 1 respectively). To register optional
whole-file benchmarks, set `DACOEXEC_BENCHMARK_INPUT` to a local dataset path and
select `--benchmark_filter=file_archive`. Input loading and, for decoding,
compression happen outside the timing loop. Real-file measurements have not
been run as part of this revision.

Archive benchmark counters report the percentage of data frames using RAW,
rANS, tANS and Huffman, plus the total frame count. These are frame percentages,
not byte percentages, and are computed from the resulting archive outside the
timed loop. Empty archives report zero for every percentage.

The ANS fuzz corpus includes complete tANS archives at logs 8, 10 and 12 built
from the reviewed skewed golden vectors. This lets ordinary libFuzzer mutations
reach model metadata, decoder state, valid-bit count and packed bits without
first discovering a valid DAN1 envelope. The optional corpus generator also
emits tANS archives from the fixture datasets for every supported log.
