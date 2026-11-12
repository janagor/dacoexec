# Cross-codec validation

The compatibility tests freeze ten DAC1 archives, including all four arithmetic
variants, RAW and END, adaptive rescaling, and context history across bytes.
The fuzz suite checks all seven codecs on identical input and separately explores
malformed DAC1 and direct arithmetic payloads under ASan/UBSan. A separate
payload round-trip target bypasses RAW fallback for all four arithmetic variants;
the archive round-trip target asserts compressed tags on its zero seed for all
seven codecs. Both targets cap plain input at 4 KiB and encode only once per
selected codec. Determinism and large rescaling cases remain in dedicated tests.

## Local measurement smoke check

The accompanying [timing CSV](cross-codec-smoke.csv) contains 98 archive cases:
seven codecs, seven shared synthetic datasets, encoding and decoding, 64 KiB
input and block size. [Allocation CSV](cross-codec-allocations.csv) additionally
covers 1 MiB blocks (196 rows). Every operation checked a complete round trip;
the timing harness also checked deterministic encoding before measurement.

Build: Clang 22.1.5, C++20, Release, unixlike-clang-bench preset, base revision
`6887b09efd5edd719e1b67b442e97a52c52e7a83` plus the validation changes. Google Benchmark v1.9.5.
Host: jg, 12 logical CPUs,
reported clock 4400 MHz. Timing date: 2026-11-13T00:42:36+09:00.

These are unpinned 0.01-second minimum smoke runs, one repetition, with CPU
scaling enabled and concurrent validation processes. They establish that all
comparison paths execute and report archive sizes; throughput is noisy and must
not be treated as a performance ranking. Run the documented repeated pinned
benchmark procedure on an otherwise idle machine before drawing conclusions.
The CSV includes complete archive overhead and RAW fallback, not just entropy
payloads. The ordinary-allocation probe reports cumulative requested storage,
not peak RSS or aligned/direct-malloc allocations.

Static/adaptive range share a byte engine. Fixed/context binary share a bit
engine, but differ in both adaptation and context selection. Comparing binary
to range changes both the alphabet and engine. These measurements cannot assign
all gains to one modeling feature. Model construction microbenchmarks remain
available separately in the benchmark targets.

## Reproducing the smoke measurements

From the repository root, the timing invocation that produced the CSV was:

```sh
cmake --preset unixlike-clang-bench
cmake --build out/build/unixlike-clang-bench \
  --target dacoexec_arithmetic_benchmarks dacoexec_codec_allocations
out/build/unixlike-clang-bench/benchmarks/dacoexec_arithmetic_benchmarks \
  --benchmark_filter='^archive/[a-z_]+_(encode|decode)/65536/[0-6]/real_time$' \
  --benchmark_min_time=0.01s \
  --benchmark_out=/tmp/daco-comparison-all.json --benchmark_out_format=json
out/build/unixlike-clang-bench/benchmarks/dacoexec_codec_allocations \
  > /tmp/daco-allocations.csv
```

The historical invocation used Google Benchmark's default repetition count of
one; `--benchmark_repetitions=1` makes it explicit for a rerun. No CPU affinity
was applied. Unset `DACOEXEC_BENCHMARK_CORPUS` to match this synthetic-only run.
The timing CSV was extracted without rounding or aggregating the JSON rows:

```sh
python3 - <<'PY'
import csv
import json

with open('/tmp/daco-comparison-all.json') as source:
    rows = json.load(source)['benchmarks']
assert len(rows) == 98
assert not any(row.get('error_occurred', False) for row in rows)
assert all(row['time_unit'] == 'ns' for row in rows)
with open('/tmp/cross-codec-smoke.csv', 'w', newline='') as output:
    writer = csv.writer(output)
    writer.writerow(['benchmark', 'dataset', 'archive_bytes', 'ratio',
                     'bytes_per_second', 'real_time_ns'])
    for row in rows:
        writer.writerow([row['name'], row['label'], row['archive_bytes'],
                         row['ratio'], row['bytes_per_second'], row['real_time']])
PY
```

The allocation CSV is the probe's stdout, copied unchanged. Reproduction checks
case counts, sizes and output format; fresh timing values will differ.
