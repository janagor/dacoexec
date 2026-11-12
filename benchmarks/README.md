# Reproducible codec measurements

`dacoexec_arithmetic_adaptive_model_benchmarks` compares the cumulative-array
and Fenwick-tree models at 64 KiB and 1 MiB. Its four deterministic inputs are repeated
`0x00`, repeated `0xFF`, uniform bytes, and skewed bytes (zero except for a
1/16 chance of a uniformly selected byte). Generation uses mt19937 seed 1729.

Each representation has five operations, registered as
`array|fenwick/operation/distribution/size` (80 cases total):

- `updates`: update frequencies, including automatic rescaling.
- `lookup`: find a symbol in a frozen, fully trained model.
- `interval_lookup`: retrieve cumulative, frequency and total using the common
  `interval_for(symbol)` interface on that frozen model.
- `encode_transition`: retrieve the current interval, then update the symbol.
- `decode_transition`: find the symbol, retrieve its interval, then update it.

Mutable operations reset to a fresh model with timing paused each iteration.
Input generation, frozen-model training, trace allocation and correctness checks
are outside timing. Seed 42 samples cumulative slots within the reference
array model's intervals. Frozen lookup uses its final state; the decoding
transition uses its changing state before every update. Both strategies thus
receive identical valid slots, including through rescaling. Every candidate's
intervals, symbols and final frequencies are checked against the reference
before timing. Checksums keep retrieved values observable. These are model-only
costs; they exclude integer range coding, framing and model initialization.

Use `encode_transition` and `decode_transition` as the primary model-selection
metrics. Frozen `interval_lookup` is supplementary sustained-throughput data,
not individual API-call latency: the compiler can hoist invariants, vectorize
the loop and overlap independent work. Sub-nanosecond per-symbol results can
reflect those optimizations. Complete adaptive range and archive measurements
are still needed before choosing the production representation.

The `seconds_per_symbol` counter displays SI units (for example, `18ns`) in the
console; multiply its JSON value by 1e9 for nanoseconds per symbol. It uses CPU
time and equals the reciprocal of `items_per_second`. Batch CPU/real times are
also retained. Compare the same distribution and batch size between strategies.

```sh
cmake --preset unixlike-clang-bench
cmake --build out/build/unixlike-clang-bench --target dacoexec_arithmetic_adaptive_model_benchmarks
taskset -c 2 out/build/unixlike-clang-bench/benchmarks/dacoexec_arithmetic_adaptive_model_benchmarks \
  --benchmark_min_time=0.5s --benchmark_repetitions=7 \
  --benchmark_enable_random_interleaving=true \
  --benchmark_out=results/adaptive-model.json --benchmark_out_format=json
```

Create `results` first and select an allowed CPU as described below. Initial
array-model measurements are recorded in [the adaptive model report](../docs/adaptive-model-benchmarks.md);
the expanded matrix is in [the representation comparison](../docs/adaptive-model-comparison.md).
Random interleaving mixes repetitions of both representations to reduce
systematic drift from execution order. Retain individual repetitions in JSON,
record CPU scaling and other workloads, and compare variability as well as
medians. The existing report used the shorter, noninterleaved command documented
there; its measurements have not been replaced by this recommended longer run.

Use `nix develop .#clang`, then:

```sh
cmake --preset unixlike-clang-bench
cmake --build out/build/unixlike-clang-bench
mkdir -p results
out/build/unixlike-clang-bench/benchmarks/dacoexec_benchmarks \
  --benchmark_repetitions=3 --benchmark_out=results/benchmarks.json \
  --benchmark_out_format=json
python3 benchmarks/compare.py results/benchmarks.json > results/comparison.json
python3 benchmarks/measure.py --build-dir out/build/unixlike-clang-bench --output results --large
```

Google Benchmark is resolved privately (existing target, installed package, then
v1.9.5 CPM fallback). The preset requires Clang 22, Release C++20, execution ON,
and disables sanitizers, static analysis, coverage, hardening and IPO. Consumers
never require Benchmark. Both measurement executables remain uninstalled.

`fixtures.hpp` uses mt19937 seed 1729 and explicit modulo selection, so generators
are reproducible across standard libraries. Datasets are uniform bytes, repeated
'a', alternating 0/1, a dominant 'a' with 1/16 random bytes, bytes sampled from a
fixed English sentence, and cycling through the complete alphabet. Stream tests
process 64 MiB; block tests use 64/256 KiB and 1/4 MiB. Setup, fixture generation,
validation, thread-pool startup, and stream precompression are outside timing.
Block compression and decompression prepare the owned input required by their
consuming APIs with timing paused. Stream measurements include
framing, CRC32, buffered block processing and a nonaccumulating in-memory sink;
they do not measure filesystem throughput. Allocation/destruction during codec
calls remains timed. Parallel compression additionally performs an untimed
preflight: collect sequential and parallel archives, compare every byte, then
decode the parallel archive and compare it to the original input. Collected
buffers are released before timed iterations use the counting sink.

Routine parallel measurements vary workers 1/2/4/8 with in-flight=4, then vary
in-flight 1/2/4/8 with workers=4. Enable `dacoexec_BENCHMARK_FULL_MATRIX=ON` for
the full matrix. The text dataset matches sequential stream measurements.
Scheduling tests compare identical frequency-counting work directly versus the
public ordered sender, including ordered commits, using the same persistent pool.
Their difference includes bookkeeping and concurrency effects; it can be negative.

Throughput uses original bytes via SetBytesProcessed. Compression ratio always
means encoded/original bytes: block results include code lengths when present;
stream results include all DHF1 framing. JSON retains wall/CPU times, repetitions
and variability; parallel CPU measurement includes all process threads.
`compare.py` derives sequential/parallel wall-time speedup and scheduled-minus-
direct wall time for matching repetitions. Missing or duplicate sequential
references, missing scheduling variants, failed benchmark rows, and empty or
renamed comparison families are errors with a nonzero exit status. Time units
are normalized before comparison. The script is regression-tested with
`python3 -B test/benchmark_compare_tests.py`. Do not compare runs with different
hardware, compiler flags, seeds, block sizes or framing definitions.

Memory is measured in fresh Linux processes with GNU `time -v` from PATH
(or `--time /path/to/time`), separately
from repeated benchmark timing. The generated source and discard sink do not
retain the whole stream. Both zero-filled and deterministic incompressible
inputs are probed at 64/512 MiB with B=1 MiB, K=4, workers=4, plus 512 MiB at
B=4 MiB, K=8, workers=8. The uniform source derives eight bytes per SplitMix64
counter with seed 1729; it allocates no stream-sized input and does not repeat
a short byte pattern. Each uniform probe asserts that every frame selected RAW
by checking its complete archive size against RAW framing overhead. The optional
more-than-4-GiB probe remains zero-filled. RSS growth is evaluated independently
for each dataset at the same B/K/workers. Invoke the probe directly with
`dacoexec_stream_memory INPUT_BYTES BLOCK_BYTES IN_FLIGHT WORKERS zero|uniform`.
The script
retains raw RSS logs, JSON measurements, compiler identity, CPU information,
logical processor count, Git SHA, dirty state and CMake cache. It rejects growth
above a 32 MiB startup/allocator allowance at fixed B/K/workers. This catches
input-proportional retention without enforcing unstable throughput thresholds.

The execution implementation stages batches. Before commit, at most K owned
inputs coexist with K worker results (Huffman results contain at most B payload
bytes, plus fixed tables/headers). Encoding also uses fixed alphabet/tree storage
per active worker; allocating vector growth may add transient capacity. Source
block staging and commit ownership add a constant number of B-sized buffers.
Thus live codec storage is O(B*K), plus O(K) codebooks/operation states, thread
stacks, scheduler overhead and allocator caching. Completed results count against
K and must be released before the next batch; existing execution tests assert
that lifecycle. RSS is a separate empirical check of that allocation model.

The manual/weekly workflow uploads measurements as artifacts. No local runtime
measurements are recorded by this change: builds and execution must be completed
before claiming the fuzz, TSan, packaging or memory acceptance criteria are met.

The `bm_ans_*` cases use the same fixture matrix and block sizes as Huffman,
including metadata in encoded size. Block compression includes frequency counting
and normalization; decoding starts from an existing block/model. Stream cases
include serialization and CRC32. Parallel cases verify byte equality against
sequential encoding before timing. `dacoexec_ans_stream_memory` accepts the same
arguments as `dacoexec_stream_memory` for separate-process peak-RSS comparisons.
No benchmark results are claimed until these executables have been built and run.

`dacoexec_arithmetic_benchmarks` provides a common static/adaptive-range, binary,
Huffman, rANS, and tANS archive comparison and isolated static-range stages.
Binary uses a two-symbol bit model and an E1/E2/E3 arithmetic engine, while
static/adaptive range use a 256-symbol byte model and a byte-oriented range
engine. These comparisons change both modeled alphabet and coding engine;
performance differences cannot be attributed solely to bit versus byte modeling.
Archive sizes and ratios include each codec's model metadata and framing. Run it with:

```sh
taskset -c 2 out/build/unixlike-clang-bench/benchmarks/dacoexec_arithmetic_benchmarks \
  --benchmark_min_time=0.5s --benchmark_repetitions=7 \
  --benchmark_out=results/arithmetic.json --benchmark_out_format=json
```

Choose a CPU allowed by `taskset -pc $$`; CPU 2 is an example. On platforms
without `taskset`, use the platform's affinity tool and record the selection.
Frequency scaling can still introduce noise; compare variation as well as
medians. Use short runs only for correctness smoke checks.

Synthetic cases use the shared fixtures (including `binary_skewed`), seed 1729,
and 64 KiB or 1 MiB of original input. The second numeric argument is the dataset
index in `fixtures.hpp`; the JSON label also names it. Archive cases use one
frame of that size and the default codec precision/options. The additional
`archive/*_multi_*` matrix uses 1 MiB inputs and a fixed 64 KiB block size
(16 data frames). Dataset 7, labeled `mixed`, cycles uniform, repeated,
alternating, and skewed 64 KiB blocks; it is measured with both single-frame
and multi-frame settings. This exercises changing models, serialization sizes,
and RAW/compressed decisions. `input_bytes`, `block_bytes`, and `frames` make
the archive configuration explicit in JSON. Before timing, each
archive is checked for deterministic encoding and a full round trip. Encoding
and decoding both report `archive_bytes` and `ratio` (complete archive bytes
divided by original bytes), including model serialization, framing, checksums,
END, and RAW fallback. Thus incompressible archives measure the selected codec's
fallback path. They are not payload-only comparisons.

The `micro/*` cases separately measure histogram counting, 12-bit normalization,
range encoding, range decoding, and model serialization. Counts, models, and
payloads needed by a stage are prepared outside timing; every range block is
round-trip checked before timing. Micro encoding/decoding bypass RAW fallback.
`model_bytes` and `payload_bytes` describe the candidate compressed block even
when a complete archive would select RAW. Allocations and destruction inside
each operation remain timed. Normalization and serialization process a fixed
256-symbol table: they report `items_per_second` (one model per item) and
nanoseconds per operation, with no `bytes_per_second` counter. Only histogram
counting and range encoding/decoding report byte throughput.

Byte-processing cases report wall-time throughput in original bytes, with CPU
time retained in JSON. Fixture generation, correctness checks, and archive precompression are
outside timing. Use the same executable and machine for all four codecs and
avoid concurrent build/test workloads. These are sequential in-memory timings;
the existing execution tests cover ordered parallel arithmetic streams. Initial
measurements and limitations are recorded in
[the static range report](../docs/static-range-benchmarks.md).

To add real data, set `DACOEXEC_BENCHMARK_CORPUS` to a directory containing four
nonempty files named `text`, `source`, `binary`, and `structured`. Their bytes
and natural lengths are used directly; files are neither resampled nor repeated.
Keep each file at most 16 MiB for the single-frame configuration. The optional
`archive_real/*` cases cover every codec, encode/decode, and single/fixed-64-KiB
blocks (64 cases). Files smaller than 64 KiB still have one frame in both modes.
File I/O occurs before timing. Missing, empty, or unreadable files fail the
benchmark preflight. Record source paths, sizes, and SHA-256 hashes with the run;
an executable's exact bytes depend on the compiler and build configuration.
Without the environment variable, only the 254 synthetic cases are registered.
The [longer pinned baseline](../docs/static-range-baseline.md) includes a corpus
manifest and keeps real-file results separate from synthetic distributions.

`bm_ans_normalize` isolates integer model normalization from counting and payload
coding. `dacoexec_ans_allocations` prints CSV allocation counts and total requested
bytes for both codecs over the same matrix, excluding input copies. Its private
ordinary `new`/`delete` replacements measure the standard-alignment allocations
used here; they do not instrument aligned allocation or third-party `malloc`.
Use separate processes for the memory probes and record peak RSS (for example
with `/usr/bin/time -v` on Linux); total allocated bytes are not peak live memory.

The archive matrix now includes `context_binary` alongside static range,
adaptive range, binary, Huffman, rANS and tANS, including real files, changing
distributions and multiple frames. Context binary uses 32 adaptive bit models
and the same binary engine as fixed binary; their difference includes context
selection, adaptation, and model update cost. Static versus adaptive range holds
the byte engine constant. Neither comparison alone isolates every modeling cost.

`dacoexec_codec_allocations` measures complete archive encoding and decoding for
all seven configurations on identical 64 KiB/1 MiB synthetic fixtures. It emits
CSV with archive bytes, allocation calls, and total requested allocation bytes.
Input generation and archive preparation for decoding are outside each measured
scope; each result is checked for exact restoration. Ordinary `new`/`delete`
hooks exclude aligned allocations and direct `malloc`; requested bytes are not
peak live memory. Run this separately from timed benchmarks:

```sh
cmake --build out/build/unixlike-clang-bench \
  --target dacoexec_arithmetic_benchmarks dacoexec_codec_allocations
out/build/unixlike-clang-bench/benchmarks/dacoexec_codec_allocations > allocations.csv
```

Keep the raw benchmark JSON and allocation CSV with compiler flags, CPU,
Git revision, block sizes, affinity and corpus hashes. Archive bytes include all
metadata, checksums, END and RAW fallback; stage counters describe candidate
payload/model sizes and must not be subtracted from RAW archives to infer
framing overhead. Old synthetic/real case counts above predate the expanded
codec matrix. Timing smoke checks establish executability, not a performance
ranking; use repeated pinned runs for conclusions.
