# Frozen DAC1 compatibility fixtures

Each `.dac` is immutable archive input; the matching `.bin` stores its decoded
bytes. The compatibility test always decodes these files and checks exact
re-encoding with the frame's coder and default options. `binary-uniform` is a
valid independently authored compressed frame smaller than the encoder's RAW
selection threshold, so only its decoding is checked.

Static, adaptive, binary, empty and RAW archives are copied from the committed
arithmetic decoder corpus. The context archives are the independent integer
reference vectors already frozen in `arithmetic_context_codec_tests.cpp`.
Decoded companions were extracted from those archives and reviewed against the
existing semantic tests; they are not an independent decoder implementation.
`adaptive-rescale-unseen` freezes the rescaling transition and introduction of
all byte values. `context-mixed` freezes bit order and history across bytes.

Do not regenerate these archives when changing an encoder. A difference must be
investigated as a compatibility regression or assigned a new format identifier.
