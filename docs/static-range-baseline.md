# Static range baseline with CPU affinity

This baseline follows the [historical short run](static-range-benchmarks.md)
with a minimum of 0.5 seconds per repetition, seven repetitions, and CPU affinity
fixed to logical CPU 2 using `taskset`. The run started on 2026-11-11 at
00:58:43 +09:00, with load averages of 1.09/1.16/1.40.
Hardware and compiler remain the Intel
Core i5-12450H, Linux 6.18.52, Clang 22.1.5, libstdc++ 15.2, Release C++20
`-O3 -DNDEBUG`, and Google Benchmark 1.9.5. Frequency scaling remains enabled;
pinning a CPU does not fix its clock or eliminate other system activity.
No build or test workload from this session ran during measurement.
Compare codecs within this run; changes from the historical rates reflect
different measurement conditions and are not decoder optimizations.

The longer run deliberately selects 85 cases: the five micro stages on 1 MiB
synthetic text, all four codecs on 1 MiB alternating/text/mixed inputs in both
single-frame and 16-frame modes, and single-frame encode/decode of four real
files for all four codecs. This is a focused baseline, not a rerun of every
synthetic distribution. All 318 registered cases, including real-file fixed-block
modes, were separately run as a short correctness smoke check before timing.
The selected cases are explicit in the command below.

## Reproduction

```sh
cmake --preset unixlike-clang-bench
cmake --build out/build/unixlike-clang-bench --target dacoexec_arithmetic_benchmarks
mkdir -p out/results/static-range-review/corpus
cp README.md out/results/static-range-review/corpus/text
cp include/dacoexec/detail/arithmetic/range_decoder.hpp out/results/static-range-review/corpus/source
cp CMakePresets.json out/results/static-range-review/corpus/structured
cp out/build/unixlike-clang-bench/benchmarks/dacoexec_arithmetic_benchmarks \
  out/results/static-range-review/corpus/binary
DACOEXEC_BENCHMARK_CORPUS=out/results/static-range-review/corpus \
taskset -c 2 out/build/unixlike-clang-bench/benchmarks/dacoexec_arithmetic_benchmarks \
  '--benchmark_filter=^(micro/[^/]+/1048576/4/|archive/[^/]+/1048576/[247]/|archive_real/(static_range|huffman|rans|tans)_(encode|decode)/)' \
  --benchmark_min_time=0.5s --benchmark_repetitions=7 \
  --benchmark_out=out/results/static-range-review/baseline.json \
  --benchmark_out_format=json
```

Check allowed CPUs with `taskset -pc $$` before choosing an affinity. The JSON
retains all individual repetitions as well as mean, median, standard deviation,
and coefficient of variation. Record file hashes when regenerating the corpus;
repository edits or a different compiler/build can change these inputs.

## Inputs and units

Synthetic fixtures use seed 1729. Dataset 2 is alternating bytes, dataset 4 is
independently sampled sentence bytes, and dataset 7 (`mixed`) cycles uniform,
repeated, alternating, and skewed distributions at 64 KiB boundaries. Single-frame
cases use a 1 MiB block; `_multi_` cases use fixed 64 KiB blocks, producing 16
frames. Mixed inputs therefore exercise repeated initialization, changing model
sizes, and both RAW and compressed blocks. `input_bytes`, `block_bytes`, and
`frames` counters identify the configuration explicitly.

Real inputs retain their natural order and length. They are small repository
samples, useful for exercising actual text, C++ source, JSON, and ELF bytes;
they do not represent a broad external corpus or justify general workload
claims. Real-file results use separate `archive_real/` names. The optional
fixed-64-KiB real-file cases are smoke-tested but excluded from the longer run.

| Corpus name | Source | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| text | README.md | 18,399 | `f1b736fba539f2823239df3a91d24ea67910371098abad71fe23829baaf8ff4b` |
| source | include/dacoexec/detail/arithmetic/range_decoder.hpp | 2,724 | `29b983a4f7595d75e508daf6246d05b43f97c30f32922dc9712e732ec5971733` |
| structured | CMakePresets.json | 10,888 | `6eddbd879313d1b9d654e378a1abadbad67e88d1ef66f7de8f6aafea22b68f0a` |
| binary | Release benchmark executable | 613,856 | `53e79337203cafd3f09b85f6fbcd8e52654271679cff07a3195136ab0bdeace4` |

Histogram counting and payload encoding/decoding report original MiB per wall
second. Normalization and serialization process one fixed-alphabet model per
iteration: they report nanoseconds per operation and `items_per_second`, with
no byte-throughput counter. In the CSV, these rates are named
`models_per_second`, and `MiB_per_second` is empty for those rows.

Archive ratio is complete archive size divided by original size. Model
serialization, framing, CRC32, END, allocation, and destruction remain timed.
Inputs, file I/O, precompression for decoding, deterministic encoding checks,
and round-trip validation are outside timing. Static range still uses the
existing cumulative-search decoder; the codec and DAC1 bytes are unchanged.

## Results

The [median results](static-range-baseline.csv) contain all 85 cases; the
[individual repetitions](static-range-baseline-repetitions.csv) contain all
595 measurements. All cases passed their untimed determinism and round-trip
checks. The maximum wall-time coefficient of variation was 7.79%.
CSV wall/CPU times are nanoseconds per operation. Median rows include wall CV
in percent; repetition rows retain the zero-based repetition index. Both files
leave byte throughput empty for fixed-cost model operations. Archive sizes and
ratios include metadata and complete framing.

For 1 MiB alternating input, the comparison highlighted in review becomes:

| Codec | Frames | Archive bytes | Ratio | Encode MiB/s | Decode MiB/s |
| --- | ---: | ---: | ---: | ---: | ---: |
| static_range | 1 | 131,120 | 0.125046 | 99.6 | 29.5 |
| static_range | 16 | 131,525 | 0.125432 | 104.6 | 30.8 |
| huffman | 1 | 131,362 | 0.125277 | 60.5 | 85.1 |
| huffman | 16 | 135,397 | 0.129125 | 62.0 | 94.2 |
| rans | 1 | 131,119 | 0.125045 | 110.9 | 129.0 |
| rans | 16 | 131,509 | 0.125417 | 114.6 | 140.4 |
| tans | 1 | 131,123 | 0.125049 | 97.5 | 140.0 |
| tans | 16 | 131,573 | 0.125478 | 95.9 | 155.8 |

The slower static-range compressed-frame decoding remains visible under these
conditions. Single-frame and multi-frame measurements share identical original
bytes, so the size difference includes repeated model/header costs. It also
reflects per-frame frequency normalization and RAW fallback decisions; it is not
a measurement of serialization alone. The CSV includes mixed-distribution and
real-file results separately, rather than combining them into an overall score.

For the 1 MiB synthetic text fixture, fixed-cost model operations measured:

| Operation | Median ns/model | Models/s |
| --- | ---: | ---: |
| normalization | 1907.5 | 524,249 |
| model_serialization | 321.2 | 3,113,465 |

## Validation

The updated executable passed all 318 registered benchmark preflights using
`--benchmark_min_time=0.001s --benchmark_repetitions=1` with the corpus environment
set and no filter. JSON checks confirmed that fixed-cost cases have model rates
and no byte rates, and every synthetic multi-frame case reports 16 frames with
64 KiB blocks. The longer run passed all 85 selected cases. Export checks
verified ratio calculations, unique repetition identities, and corpus hashes.

The existing test executables were rerun after measurement: all 247 core cases
and all 41 execution cases passed. They can be reproduced with:

```sh
cmake --build out/build/unixlike-clang-debug --target dacoexec_tests dacoexec_execution_tests
out/build/unixlike-clang-debug/test/dacoexec_tests
out/build/unixlike-clang-debug/test/dacoexec_execution_tests
```
