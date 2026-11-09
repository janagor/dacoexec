# Huffman fuzzing

Configure with `cmake --preset unixlike-clang-fuzz`, build that binary directory,
and run `ctest --test-dir out/build/unixlike-clang-fuzz -L fuzz -V` in the Nix
Clang shell. Requires Clang 22 and compiler-rt. Instrumentation remains private
to the three executables; neither stdexec nor a test framework is needed.

All harnesses cap input at 64 KiB. Decoder output is limited to 64 KiB; framing
limits each frame to 64 KiB and total output to 256 KiB. Documented decode and
format errors are expected rejections. Unexpected exceptions, assertion failures,
resource failures and sanitizer diagnostics fail the run. The decoder and parser
harnesses execute only their decoding paths. A separate
`dacoexec_fuzz_round_trip` target checks deterministic block encoding, exact block
round trips, and DHF1 round trips using 1,024-byte frames. It does not catch
expected decode errors: a valid encoded input must always decode. The separate
corpus, process and coverage feedback keep encoder work out of malformed-input
exploration.

The existing Catch2 codec tests cover maximal canonical width and larger blocks.

Decoder seeds use a *harness envelope*, not DHF1: one encoding byte (RAW=0,
canonical=1), one padding byte, eight little-endian output-size bytes, 256 length
bytes, then payload. Missing fields are zero-filled. Frame seeds are DHF1
archives; round-trip seeds are plain input bytes. The parser requires END and
rejects trailing bytes and bad CRC32.

The committed bootstrap corpus includes the independent DHF1 golden fixtures,
RAW alphabet, repeated and alternating symbols, skewed data, maximum-width
canonical code, complete 255/256-symbol tables, singleton with partial final byte, truncated data, oversubscribed
and unsupported widths, and an enormous declared size. These fixtures are
independently authored, not claimed as encoder-generated output. To regenerate
additional valid seeds using the actual encoder:

```sh
cmake --build out/build/unixlike-clang-fuzz --target dacoexec_generate_corpus
out/build/unixlike-clang-fuzz/fuzz_test/dacoexec_generate_corpus fuzz_test/corpus
```

This adds fixed-seed repeated, alternating, uniform, skewed, text-like and full
alphabet cases for all three harnesses. Generated frame archives use 256- and
1,024-byte blocks, producing multiple DATA frames and a partial final block from
4,097-byte inputs. The committed `multi-frame-mixed` seed composes existing
canonical and RAW golden DATA frames with a single END; its truncated companion
exercises a later failure. These composed seeds are independent fixtures, not
claimed as output from the unexecuted corpus utility. Review and commit newly
generated seeds after running the encoder utility.
CTest writes discoveries to the build tree and keeps source seeds unchanged.
CI runs 30 seconds per target on PRs and 300 seconds on scheduled/manual runs;
logs retain libFuzzer execution/coverage statistics and discovered corpus files.
These budgets provide evidence, not a proof of bug absence.

For a failure, preserve the original artifact, replay it directly, and minimize:

```sh
out/build/unixlike-clang-fuzz/fuzz_test/dacoexec_fuzz_frame_parser crash-input
out/build/unixlike-clang-fuzz/fuzz_test/dacoexec_fuzz_frame_parser \
  -minimize_crash=1 -exact_artifact_path=minimized crash-input
```

Replay the minimized case before and after the fix, commit it under the relevant
corpus, and add a focused Catch2 regression for meaningful semantic defects.
Merge corpora with libFuzzer `-merge=1 destination sources...`; do not commit
unreviewed generated corpora. All three binaries accept individual files for replay.

`ans_decoder` exercises bounded DAN1 decompression and rANS and tANS block decoding.
Its corpus includes empty, RAW, and single-symbol compressed archives. Output is
limited to 64 KiB so malformed declarations cannot request unbounded allocations.
