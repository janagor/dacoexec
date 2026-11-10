# Adaptive model representation comparison

The array and Fenwick implementations expose the same `interval_for(symbol)`,
`find_symbol(slot)`, `update(symbol)` and `total()` operations. The frequency
initialization, update order, threshold and rescaling rule are shared. The
Fenwick representation is an internal alternative; the array remains the
default. Both own fixed-size storage and allocate no memory.

The benchmark includes three isolated operations and two model transitions:

| Operation | Timed work |
| --- | --- |
| `updates` | Update and automatic rescaling |
| `lookup` | Find symbol in a frozen trained model |
| `interval_lookup` | Retrieve encoding interval in a frozen trained model |
| `encode_transition` | Retrieve current interval, then update |
| `decode_transition` | Find symbol, retrieve current interval, then update |

Transition cases start from uniform frequencies each iteration, with reset
outside timing. The reference array model generates slots before each update;
both representations consume that identical evolving-model trace. Seed 42
selects offsets inside the current intervals. The timed decoding transition
contains no RNG or reference-model work. Frozen lookup uses slots from the
fully trained reference model. Candidate intervals, lookups and final frequency
tables are validated before timing. Input generation uses seed 1729 and the
same four distributions as the [original baseline](adaptive-model-benchmarks.md).

Interval and transition cases checksum all retrieved interval fields; reverse
lookup also retains its symbol. These loop and checksum costs are included,
while allocation, training, initialization, validation and integer range coding
are excluded. The combined results measure the model workload and do not equal
the sum of the isolated measurements.

Use `encode_transition` and `decode_transition` as the primary model-selection
metrics. Frozen `interval_lookup` measures sustained throughput over independent
queries, not standalone API-call latency. Invariant hoisting, vectorization and
overlapping work can amortize its cost; the array's roughly 0.32 ns/symbol is
supplementary diagnostic data. Full adaptive range encode/decode and archive
benchmarks remain necessary to choose the production model.

The run used an Intel Core i5-12450H, Linux x86-64, Clang 22.1.5, C++20,
Release `-O3 -DNDEBUG`, no IPO or sanitizers, pinned to logical CPU 2 on
2026-11-11. CPU scaling was enabled. No concurrent build or test work was run.
All 80 configurations used three repetitions with a minimum of 0.1 CPU seconds
per repetition. These short measurements characterize this implementation on
this machine; longer runs are appropriate before selecting a codec policy.
The repetitions were not randomly interleaved, so thermal and frequency drift
may also affect comparisons between representations.

The results show a tradeoff. At 1 MiB, Fenwick improves the encoding transition
for zero, uniform and skewed inputs, but slows the `0xFF` case. Uniform decoding
takes 40.968 ns/symbol with Fenwick versus 36.843 with the array; `0xFF` decoding
also slows, while zero and skewed decoding improve. Prefix retrieval costs up
to 7.203 ns/symbol for Fenwick versus roughly 0.32 for the array. Thus faster
updates alone do not establish a better model for both encoding and decoding.
Retain the array default pending complete adaptive-codec measurements.

Median CPU nanoseconds per symbol for 1 MiB workloads:

| Operation | Distribution | Array | Fenwick |
| --- | --- | ---: | ---: |
| `updates` | zero | 19.208 | 4.186 |
| `updates` | ff | 1.856 | 1.996 |
| `updates` | uniform | 19.736 | 9.836 |
| `updates` | skewed | 19.194 | 5.396 |
| `lookup` | zero | 7.938 | 5.325 |
| `lookup` | ff | 6.898 | 5.880 |
| `lookup` | uniform | 7.018 | 29.352 |
| `lookup` | skewed | 9.128 | 7.105 |
| `interval_lookup` | zero | 0.330 | 0.441 |
| `interval_lookup` | ff | 0.323 | 2.648 |
| `interval_lookup` | uniform | 0.324 | 7.203 |
| `interval_lookup` | skewed | 0.324 | 1.416 |
| `encode_transition` | zero | 20.211 | 4.771 |
| `encode_transition` | ff | 1.682 | 3.090 |
| `encode_transition` | uniform | 19.744 | 10.667 |
| `encode_transition` | skewed | 20.113 | 5.516 |
| `decode_transition` | zero | 35.378 | 21.612 |
| `decode_transition` | ff | 8.002 | 15.354 |
| `decode_transition` | uniform | 36.843 | 40.968 |
| `decode_transition` | skewed | 36.185 | 22.907 |

[Individual repetitions](adaptive-model-comparison.csv) retain all 240 rows
including 64 KiB workloads, iteration counts and CPU/wall batch times. The
`seconds_per_symbol` JSON counter is converted to nanoseconds in this report
and equals the reciprocal of `items_per_second`.

Reproduce after building the target as described in
[the benchmark guide](../benchmarks/README.md):

```sh
taskset -c 2 out/build/unixlike-clang-bench/benchmarks/dacoexec_arithmetic_adaptive_model_benchmarks \
  --benchmark_min_time=0.1s --benchmark_repetitions=3 \
  --benchmark_display_aggregates_only=true \
  --benchmark_out=/tmp/dacoexec-adaptive-comparison.json --benchmark_out_format=json
```

For a more stable follow-up, use at least seven repetitions of 0.5 seconds or
longer and add `--benchmark_enable_random_interleaving=true`, as shown in the
benchmark guide. This mixes repetitions across the array and Fenwick cases.
Keep the full per-repetition output, record CPU scaling and compare variation.
The table above retains the original short-run measurements; this follow-up
has not been run for the report.

Tests apply the same boundary, rounding, frequency-history and range-engine
synchronization checks to both representations. Additional tests compare
random slots after each update and every slot at checkpoints across repeated
rescaling. Both encoders must produce identical range payloads and both decoders
must restore the input from those payloads.
