#!/usr/bin/env python3
"""Run separate RSS probes and retain machine/build metadata alongside benchmark JSON."""
import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess

MIB = 1024 * 1024
GIB = 1024 * MIB
RSS_SLACK_KIB = 32 * 1024


def capture(*args):
    return subprocess.check_output(args, text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--large', action='store_true')
    parser.add_argument('--time', default=shutil.which('time'), help='GNU time executable (defaults to PATH)')
    args = parser.parse_args()
    if not args.time:
        parser.error('GNU time is required; install it or supply --time /path/to/time')
    args.output.mkdir(parents=True, exist_ok=True)
    cache = (args.build_dir / 'CMakeCache.txt').read_text()
    compiler = next(line.split('=', 1)[1] for line in cache.splitlines()
                    if line.startswith('CMAKE_CXX_COMPILER:'))
    metadata = {
        'commit': capture('git', 'rev-parse', 'HEAD'),
        'dirty': bool(capture('git', 'status', '--porcelain')),
        'compiler': capture(compiler, '--version'),
        'platform': platform.platform(), 'logical_processors': os.cpu_count(),
        'time_executable': args.time, 'time_version': capture(args.time, '--version'),
        'cpu': capture('lscpu'), 'seed': 1729,
        'memory_datasets': ['zero', 'uniform (counter-based SplitMix64, seed 1729)'],
        'memory_sink': 'nonaccumulating',
    }
    (args.output / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
    (args.output / 'CMakeCache.txt').write_text(cache)
    datasets = ('zero', 'uniform')
    configurations = [(size, MIB, 4, 4, dataset)
                      for dataset in datasets for size in (64 * MIB, 512 * MIB)]
    if args.large:
        configurations.append((4 * GIB + 4096, MIB, 4, 4, 'zero'))
    configurations.extend((512 * MIB, 4 * MIB, 8, 8, dataset) for dataset in datasets)
    measurements = []
    for index, (size, block, capacity, workers, dataset) in enumerate(configurations):
        log = args.output / f'rss-{index}-{dataset}.txt'
        run = subprocess.run([args.time, '-v', '-o', str(log),
                              str(args.build_dir / 'benchmarks/dacoexec_stream_memory'),
                              *map(str, (size, block, capacity, workers)), dataset],
                             text=True, capture_output=True, check=True,
                             env={**os.environ, 'LC_ALL': 'C'})
        peak = next(int(line.split(':', 1)[1]) for line in log.read_text().splitlines()
                    if 'Maximum resident set size (kbytes)' in line)
        measurements.append(dict(input_bytes=size, block_bytes=block, in_flight=capacity,
                                 workers=workers, dataset=dataset, peak_rss_kib=peak,
                                 output_bytes=int(run.stdout)))
        (args.output / 'memory.json').write_text(json.dumps(measurements, indent=2) + '\n')
    for dataset in datasets:
        comparable = [row for row in measurements if row['dataset'] == dataset
                      and (row['block_bytes'], row['in_flight'], row['workers']) == (MIB, 4, 4)]
        baseline = comparable[0]['peak_rss_kib']
        if any(row['peak_rss_kib'] > baseline + RSS_SLACK_KIB for row in comparable[1:]):
            raise SystemExit(f'Working memory grows with total {dataset} input; inspect saved RSS logs')


if __name__ == '__main__':
    main()
