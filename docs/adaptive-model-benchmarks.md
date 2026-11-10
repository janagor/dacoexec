# Adaptive byte model baseline

This records the original array-only matrix. The executable has since gained
interval and combined-transition cases plus a Fenwick representation; current
measurements and the updated methodology are in
[the representation comparison](adaptive-model-comparison.md). Keep this
historical run separate from the expanded matrix.

The cumulative-array implementation updates up to 255 cumulative entries per
symbol. Before selecting an alternative, the dedicated
`dacoexec_arithmetic_adaptive_model_benchmarks` executable measures that cost
separately from symbol lookup and range coding. The methodology and longer-run
command are in [the benchmark guide](../benchmarks/README.md).

This preliminary run used an Intel Core i5-12450H, Linux x86-64, Clang 22.1.5,
C++20, Release `-O3 -DNDEBUG`, no IPO or sanitizers, pinned to logical CPU 2.
It ran on 2026-11-11 with CPU scaling enabled. Each of the 16 cases used three
repetitions with a minimum of 0.1 CPU seconds per repetition. No build or test
work ran concurrently with the measurements. These short measurements are a
baseline for comparisons on this machine, not portable throughput guarantees.

Median CPU nanoseconds per symbol:

| Distribution | Update, 64 KiB | Update, 1 MiB | Lookup, 64 KiB | Lookup, 1 MiB |
| --- | ---: | ---: | ---: | ---: |
| Repeated `0x00` | 17.650 | 17.702 | 8.111 | 8.074 |
| Repeated `0xFF` | 1.785 | 1.727 | 7.043 | 7.010 |
| Uniform | 18.300 | 18.494 | 7.100 | 7.121 |
| Skewed toward zero | 17.646 | 17.643 | 9.059 | 9.111 |

[Individual repetitions](adaptive-model-benchmarks.csv) retain CPU and wall
batch times, iteration counts and CPU nanoseconds per symbol. The JSON counter
is CPU seconds per symbol; the report converts it to nanoseconds. Reproduce with:

```sh
taskset -c 2 out/build/unixlike-clang-bench/benchmarks/dacoexec_arithmetic_adaptive_model_benchmarks \
  --benchmark_min_time=0.1s --benchmark_repetitions=3 \
  --benchmark_display_aggregates_only=true \
  --benchmark_out=/tmp/dacoexec-adaptive-model.json --benchmark_out_format=json
```

At 1 MiB, zero and uniform updates cost roughly ten times as much as `0xFF`
updates. The latter avoids cumulative suffix increments, though periodic
rescaling remains timed in all cases. Uniform updates are slightly slower than
zero updates despite touching fewer entries; entry counts alone do not predict
runtime; vectorization and variable loop lengths may also affect the cost.

Lookup uses a frozen model trained on the input and slots sampled inside that
input's symbol intervals. It does not include updates. These separate timings
cannot establish complete adaptive codec throughput or prove that a Fenwick
tree or grouped table would be faster. Compare such implementations under this
same matrix, including the cost of obtaining an encoding interval, before
selecting the codec's final model representation.
