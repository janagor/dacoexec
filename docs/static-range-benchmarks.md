# Static range measurements

These are historical short-run measurements. Use the
[longer pinned baseline](static-range-baseline.md) for subsequent performance
comparisons. It adds multi-frame archives and separate real-file samples.

Measured on 2026-11-11 with an Intel Core i5-12450H (12 logical CPUs), Linux
6.18.52, Clang 22.1.5, libstdc++ 15.2, C++20, and Google Benchmark 1.9.5.
The `unixlike-clang-bench` preset uses Release `-O3 -DNDEBUG`, without
sanitizers, hardening, IPO, or architecture-specific optimization flags.
Base revision: `26597d6237a26ee4dd8389d117bcbb434617c67c`, with the benchmark
and test additions in this change. Codec implementation headers are unchanged.

The [complete CSV](static-range-benchmarks.csv) records all 182 cases: medians
of wall time, CPU time, original-byte throughput, encoded sizes, and wall-time
coefficient of variation across three repetitions. These are preliminary local
measurements, with a minimum of 20 ms per repetition. CPU scaling was enabled,
affinity was not pinned, and initial system load averages were 2.98/2.77/1.91.
No build or test workloads from this session ran during measurement. The largest
wall-time coefficient of variation was 15.4%; small differences are not evidence
of a performance improvement.
The historical CSV now leaves byte throughput empty for normalization and
serialization and derives `models_per_second` from their measured wall latency;
the original timing and archive sizes are preserved.

## Reproduction and definitions

```sh
cmake --preset unixlike-clang-bench
cmake --build out/build/unixlike-clang-bench --target dacoexec_arithmetic_benchmarks
mkdir -p out/results/static-range
out/build/unixlike-clang-bench/benchmarks/dacoexec_arithmetic_benchmarks \
  '--benchmark_filter=^(micro/|archive/(static_range|huffman|rans|tans)_(encode|decode)/[0-9]+/[0-6]/)' \
  --benchmark_min_time=0.02s --benchmark_repetitions=3 \
  --benchmark_report_aggregates_only=true \
  --benchmark_out=out/results/static-range/arithmetic.json \
  --benchmark_out_format=json
```

The JSON retains mean, median, standard deviation, and coefficient of variation
for each case. CSV rows select `aggregate_name == "median"`, convert
`bytes_per_second` to MiB/s by dividing by 1048576, and take wall CV from the
matching `aggregate_name == "cv"` row, multiplied by 100. Empty CSV size fields
are inapplicable. Failed cases are errors; all 182 cases passed their untimed
correctness checks in this run.

All codecs receive identical [fixtures](../benchmarks/fixtures.hpp), seed 1729,
and block sizes of 64 KiB and 1 MiB. Inputs are synthetic: `text` samples bytes
from a fixed sentence rather than preserving natural text order. Codec options
use their defaults, with block size set to the entire input, yielding one data
frame per archive. Ratio means **complete archive bytes / original bytes**;
it includes metadata, framing, CRC32, and END. Lower ratios are smaller.
Uniform and cycling-alphabet archives select RAW for every codec. Their decode
rates measure RAW copying and verification; compression still includes the
codec's attempt and fallback decision.

These are sequential in-memory API calls. Fixture generation, deterministic
encoding checks, and round-trip validation are outside timing. Decoding begins
from a prepared archive. Allocations, destruction, model construction, model
serialization, checksums, and frame verification inside archive operations are
timed. The [benchmark guide](../benchmarks/README.md) describes the isolated
stages and the existing parallel benchmarks separately.

## Complete archives, 1 MiB input

Throughput is original MiB per wall-clock second. The CSV also contains all
64 KiB results and variation for every row.

| Dataset | Codec | Archive bytes | Ratio | Encode MiB/s | Decode MiB/s |
| --- | --- | ---: | ---: | ---: | ---: |
| uniform | static range | 1,048,610 | 1.000032 | 76.1 | 174.6 |
| uniform | Huffman | 1,048,610 | 1.000032 | 178.0 | 189.8 |
| uniform | rANS | 1,048,610 | 1.000032 | 84.9 | 174.6 |
| uniform | tANS | 1,048,610 | 1.000032 | 78.5 | 182.4 |
| repeated | static range | 45 | 0.000043 | 87.4 | 25.0 |
| repeated | Huffman | 131,362 | 0.125277 | 47.0 | 69.2 |
| repeated | rANS | 44 | 0.000042 | 88.3 | 125.2 |
| repeated | tANS | 48 | 0.000046 | 85.0 | 139.8 |
| alternating | static range | 131,120 | 0.125046 | 77.4 | 23.6 |
| alternating | Huffman | 131,362 | 0.125277 | 48.7 | 67.8 |
| alternating | rANS | 131,119 | 0.125045 | 88.7 | 110.5 |
| alternating | tANS | 131,123 | 0.125049 | 82.2 | 120.6 |
| skewed | static range | 77,131 | 0.073558 | 76.5 | 23.6 |
| skewed | Huffman | 164,149 | 0.156545 | 46.0 | 61.7 |
| skewed | rANS | 77,126 | 0.073553 | 75.9 | 105.5 |
| skewed | tANS | 77,453 | 0.073865 | 76.2 | 117.2 |
| text | static range | 570,553 | 0.544122 | 67.5 | 21.6 |
| text | Huffman | 576,040 | 0.549355 | 37.4 | 22.1 |
| text | rANS | 570,548 | 0.544117 | 70.0 | 76.6 |
| text | tANS | 570,615 | 0.544181 | 66.2 | 83.5 |
| alphabet | static range | 1,048,610 | 1.000032 | 78.1 | 172.8 |
| alphabet | Huffman | 1,048,610 | 1.000032 | 182.3 | 180.5 |
| alphabet | rANS | 1,048,610 | 1.000032 | 84.1 | 178.5 |
| alphabet | tANS | 1,048,610 | 1.000032 | 80.2 | 181.0 |
| binary_skewed | static range | 44,279 | 0.042228 | 81.5 | 22.3 |
| binary_skewed | Huffman | 131,362 | 0.125277 | 45.9 | 70.2 |
| binary_skewed | rANS | 44,274 | 0.042223 | 80.1 | 113.0 |
| binary_skewed | tANS | 44,279 | 0.042228 | 79.5 | 133.3 |

Static range produced sizes close to rANS on these fixtures, with materially
smaller archives than Huffman on skewed distributions. Its production decoder
uses cumulative binary search and was substantially slower than ANS decoding
on compressed frames. This run measures current implementations; it does not
isolate lookup strategy as the cause. The separate
[lookup experiment](range-coding.md#static-order-0-model) compares search and
direct tables without changing the encoded bytes.

## Isolated static range stages

For the same 1 MiB `text` input, the normalized model occupies 90 bytes and the
payload occupies 570,429 bytes. The complete archive is 570,553 bytes: 90 model
bytes plus 34 framing/checksum/END bytes above the payload.

| Stage | Median time per operation | Original MiB/s |
| --- | ---: | ---: |
| Histogram counting | 506.016 us | 1,976.2 |
| Frequency normalization | 2.268 us | — |
| Range encoding with prepared model | 9,591.435 us | 104.3 |
| Range decoding with prepared model | 38,350.664 us | 26.1 |
| Model serialization | 0.360 us | — |

Counting, normalization, payload coding, and serialization are measured
separately with dependencies prepared outside each timed loop. Encoding and
decoding include their output allocation/destruction and termination handling.
Normalization and serialization operate on a fixed alphabet and report time
per model and models per second, with byte throughput left empty in the CSV.
Micro payload coding bypasses RAW fallback, even on incompressible data.

## Validation

The core test executable passed all 247 cases; the execution executable passed
all 41 cases. AddressSanitizer and UndefinedBehaviorSanitizer also passed all
33 arithmetic core cases and all 41 execution cases. Leak detection was disabled
because LeakSanitizer cannot operate under the environment's ptrace setup.

Coverage includes independent arithmetic interval checks, normalization
boundaries, long deferred carries, finalization, frozen engine and archive
bytes, model validation, corruption rejection before frame publication,
round trips, sequential streams, views, and bounded ordered parallel streams.
See [the range engine documentation](range-coding.md) for the independent
oracle and compatibility fixtures. These additions preserve the existing
static range encoding behavior and DAC1 bytes.
