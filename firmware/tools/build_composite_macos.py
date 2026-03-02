#!/usr/bin/env python3
"""
Build composite bootloader+app binary on macOS when hex2dfu (Linux x86-64) is unavailable.

Concatenates OpenBLT bootloader (padded to 8KB) with application binary, then
fixes the OpenBLT vector table checksum at 0x1C.

Usage: build_composite_macos.py <bootloader.bin> <app.bin> <output.bin> [output.hex]
"""

import struct
import sys

BOOTLOADER_SIZE = 8192  # 8KB
APP_START_OFFSET = 8192
CHECKSUM_OFFSET = 0x1C
FLASH_OFFSET = 0x08000000


def bin_to_intelhex(data: bytes, base_addr: int) -> str:
    """Convert binary to Intel HEX format."""
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i : i + 16]
        addr = base_addr + i
        hex_data = chunk.hex().upper()
        checksum = (0x100 - (len(chunk) + (addr >> 8) + (addr & 0xFF) + sum(chunk)) % 0x100) % 0x100
        line = f":{len(chunk):02X}{addr:04X}00{hex_data}{checksum:02X}"
        lines.append(line)
    lines.append(":00000001FF")  # end-of-file
    return "\n".join(lines)


def main():
    if len(sys.argv) < 4:
        print(__doc__, file=sys.stderr)
        sys.exit(1)
    bl_path, app_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
    hex_path = sys.argv[4] if len(sys.argv) > 4 else None

    with open(bl_path, "rb") as f:
        bl = f.read()
    if len(bl) > BOOTLOADER_SIZE:
        print(f"Error: bootloader ({len(bl)} bytes) exceeds {BOOTLOADER_SIZE}", file=sys.stderr)
        sys.exit(1)
    bl_padded = bl + b"\xFF" * (BOOTLOADER_SIZE - len(bl))

    with open(app_path, "rb") as f:
        app = f.read()

    composite = bl_padded + app
    with open(out_path, "wb") as f:
        f.write(composite)

    # Fix OpenBLT vector table checksum
    f = open(out_path, "r+b")
    f.seek(APP_START_OFFSET)
    words = [struct.unpack("<I", f.read(4))[0] for _ in range(8)]
    f.close()
    s = sum(words[:7]) & 0xFFFFFFFF
    expected_cs = (0xFFFFFFFF & (~s + 1))
    with open(out_path, "r+b") as f:
        f.seek(APP_START_OFFSET + CHECKSUM_OFFSET)
        f.write(struct.pack("<I", expected_cs))

    print(f"OK: composite written to {out_path} ({len(composite)} bytes)")

    if hex_path:
        with open(hex_path, "w") as f:
            f.write(bin_to_intelhex(composite, FLASH_OFFSET))
        print(f"OK: Intel HEX written to {hex_path}")


if __name__ == "__main__":
    main()
