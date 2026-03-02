#!/usr/bin/env python3
"""
Fix OpenBLT vector table checksum in composite bootloader+app binary.

The bootloader expects a two's-complement checksum at offset 0x1C in the
application's vector table (first 8 words). Sum of first 7 words + checksum
must equal 0 (mod 2^32). GCC 11.3 may produce a different layout than hex2dfu
expects, so we recompute and patch the checksum after the composite image
is built.

Usage: fix_openblt_checksum.py <bin_file>
"""

import struct
import sys

APP_START_OFFSET = 8192  # 8KB bootloader
CHECKSUM_OFFSET = 0x1C  # Within vector table


def fix_checksum(bin_path: str) -> bool:
    with open(bin_path, "r+b") as f:
        f.seek(APP_START_OFFSET)
        words = [struct.unpack("<I", f.read(4))[0] for _ in range(8)]
    s = sum(words[:7]) & 0xFFFFFFFF
    expected_cs = (0xFFFFFFFF & (~s + 1))
    if words[7] == expected_cs:
        return True  # Already correct
    with open(bin_path, "r+b") as f:
        f.seek(APP_START_OFFSET + CHECKSUM_OFFSET)
        f.write(struct.pack("<I", expected_cs))
    return True


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        sys.exit(1)
    fix_checksum(sys.argv[1])
    print("OK: checksum patched at app vector table 0x1C")


if __name__ == "__main__":
    main()
