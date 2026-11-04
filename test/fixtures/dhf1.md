# DHF1 golden fixtures

`dhf1-empty.dhf` is exactly 21 bytes, the DHF1 header followed by END.
`dhf1-one-byte.dhf` contains one RAW byte, ASCII x. Its CRC32 is 0x8CDC1683.

These fixtures were authored from the wire specification using little-endian
integer packing and IEEE CRC32, independently of the C++ implementation.

`cli-input.bin` contains all 256 byte values in ascending order for binary CLI tests.
