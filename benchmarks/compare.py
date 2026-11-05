#!/usr/bin/env python3
"""Derive complete speedup and scheduling comparisons, rejecting missing references."""
import json
import math
import sys

PARALLEL = 'bm_huffman_compress_parallel'
SEQUENTIAL = 'bm_huffman_compress_stream'
SCHEDULING = 'bm_huffman_scheduling_overhead'
NON_COMPARISON_FAMILIES = {'bm_huffman_compress_block', 'bm_huffman_decompress_block', 'bm_huffman_decompress_stream'}
FIELDS = ('block_bytes', 'dataset', 'workers', 'in_flight')
TIME_SCALES = {'ns': 1e-9, 'us': 1e-6, 'ms': 1e-3, 's': 1.0}


def measurement(row):
    parts = row['run_name'].split('/')
    parameters = {}
    for part in parts[1:]:
        if ':' in part:
            name, value = part.split(':', 1)
            if name in parameters:
                raise ValueError(f"Duplicate parameter {name} in {row['run_name']}")
            parameters[name] = value
    fields = FIELDS[:2] if parts[0] == SEQUENTIAL else FIELDS
    for field in fields:
        if field not in parameters:
            raise ValueError(f"Missing {field} in {row['run_name']}")
    seconds = float(row['real_time']) * TIME_SCALES[row['time_unit']]
    if not math.isfinite(seconds) or seconds <= 0:
        raise ValueError(f"Invalid wall time in {row['run_name']}")
    return parts[0], parameters, row.get('repetition_index', 0), seconds


def compare(document):
    sequential = {}
    parallel = []
    scheduling = {}
    for row in document['benchmarks']:
        if row.get('run_type') == 'aggregate':
            continue
        if row.get('error_occurred'):
            raise ValueError(f"Benchmark failed: {row.get('name', '<unnamed>')}: {row.get('error_message', '')}")
        family = row['run_name'].split('/', 1)[0]
        if family in NON_COMPARISON_FAMILIES:
            continue
        if family not in (SEQUENTIAL, PARALLEL, SCHEDULING):
            raise ValueError(f"Unknown benchmark family {family}; update the comparison mapping")
        family, parameters, repetition, seconds = measurement(row)
        if family == PARALLEL:
            parallel.append((row, parameters, repetition, seconds))
            continue
        fields = FIELDS[:2] if family == SEQUENTIAL else FIELDS
        key = tuple(parameters[field] for field in fields) + (repetition,)
        if family == SEQUENTIAL:
            if key in sequential:
                raise ValueError(f'Duplicate sequential measurement for {key}')
            sequential[key] = seconds
        else:
            variant = parameters.get('scheduled')
            if variant not in ('0', '1'):
                raise ValueError(f"Missing or invalid scheduled variant in {row['run_name']}")
            variants = scheduling.setdefault(key, {})
            if variant in variants:
                raise ValueError(f'Duplicate scheduling variant {variant} for {key}')
            variants[variant] = (row, seconds)
    if not parallel:
        raise ValueError('No parallel compression measurements found; check benchmark names and filters')
    if not scheduling:
        raise ValueError('No scheduling measurements found; check benchmark names and filters')
    derived = []
    for row, parameters, repetition, seconds in parallel:
        key = tuple(parameters[field] for field in FIELDS[:2]) + (repetition,)
        if key not in sequential:
            raise ValueError(f"Missing sequential reference for {row['run_name']}, repetition {repetition}")
        derived.append(dict(name=row['name'], repetition_index=repetition, speedup=sequential[key] / seconds))
    for key, variants in scheduling.items():
        if set(variants) != {'0', '1'}:
            missing = sorted({'0', '1'} - set(variants))
            raise ValueError(f'Missing scheduling variant(s) {missing} for {key}')
        row, scheduled = variants['1']
        _, direct = variants['0']
        derived.append(dict(name=row['name'], repetition_index=key[-1],
                            scheduling_delta=(scheduled - direct) / TIME_SCALES[row['time_unit']],
                            time_unit=row['time_unit']))
    return derived


def main():
    try:
        with open(sys.argv[1], encoding='utf-8') as source:
            result = compare(json.load(source))
    except (OSError, ValueError, KeyError, TypeError, IndexError) as error:
        print(f'Benchmark comparison failed: {error}', file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
