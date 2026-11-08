# Reproducible codec measurements

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

`bm_ans_normalize` isolates integer model normalization from counting and payload
coding. `dacoexec_ans_allocations` prints CSV allocation counts and total requested
bytes for both codecs over the same matrix, excluding input copies. Its private
ordinary `new`/`delete` replacements measure the standard-alignment allocations
used here; they do not instrument aligned allocation or third-party `malloc`.
Use separate processes for the memory probes and record peak RSS (for example
with `/usr/bin/time -v` on Linux); total allocated bytes are not peak live memory.
