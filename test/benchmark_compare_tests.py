"""Regression tests for complete benchmark comparisons; no codec build required."""
import copy
from pathlib import Path
import runpy
import unittest

compare = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'benchmarks/compare.py'))['compare']


def results():
    names = [
        'bm_huffman_compress_stream/block_bytes:65536/dataset:4/real_time',
        'bm_huffman_compress_parallel/block_bytes:65536/dataset:4/workers:4/in_flight:4/process_time/real_time',
        'bm_huffman_scheduling_overhead/block_bytes:65536/dataset:4/workers:4/in_flight:4/scheduled:0/process_time/real_time',
        'bm_huffman_scheduling_overhead/block_bytes:65536/dataset:4/workers:4/in_flight:4/scheduled:1/process_time/real_time',
    ]
    return dict(benchmarks=[dict(name=name, run_name=name, run_type='iteration', repetition_index=0,
                                 real_time=time, time_unit='ns') for name, time in zip(names, (20, 10, 3, 4))])


class ComparisonTests(unittest.TestCase):
    def test_complete_measurements(self):
        derived = compare(results())
        self.assertEqual(derived[0]['speedup'], 2)
        self.assertAlmostEqual(derived[1]['scheduling_delta'], 1)

    def test_missing_each_reference(self):
        for index, diagnostic in ((0, 'Missing sequential'), (2, 'Missing scheduling'), (3, 'Missing scheduling')):
            with self.subTest(index=index):
                document = results()
                del document['benchmarks'][index]
                with self.assertRaisesRegex(ValueError, diagnostic):
                    compare(document)

    def test_empty_and_renamed_families_fail(self):
        with self.assertRaisesRegex(ValueError, 'No parallel'):
            compare(dict(benchmarks=[]))
        document = results()
        document['benchmarks'][1]['run_name'] = 'renamed'
        with self.assertRaisesRegex(ValueError, 'Unknown benchmark family'):
            compare(document)

    def test_failed_measurement_is_not_silently_dropped(self):
        document = results()
        document['benchmarks'][1]['error_occurred'] = True
        with self.assertRaisesRegex(ValueError, 'Benchmark failed'):
            compare(document)

    def test_repetitions_match_independently(self):
        document = results()
        repeated = copy.deepcopy(document['benchmarks'])
        for row in repeated:
            row['repetition_index'] = 1
        document['benchmarks'].extend(repeated)
        self.assertEqual(len(compare(document)), 4)
        del document['benchmarks'][4]
        with self.assertRaisesRegex(ValueError, 'Missing sequential'):
            compare(document)

    def test_units_are_normalized(self):
        document = results()
        document['benchmarks'][0].update(real_time=0.02, time_unit='us')
        self.assertEqual(compare(document)[0]['speedup'], 2)

    def test_duplicate_reference_fails(self):
        document = results()
        document['benchmarks'].append(copy.deepcopy(document['benchmarks'][0]))
        with self.assertRaisesRegex(ValueError, 'Duplicate sequential'):
            compare(document)


if __name__ == '__main__':
    unittest.main()
