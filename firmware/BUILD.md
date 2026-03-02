# Building Wideband Firmware (F1 Dual Rev1)

Instructions for building the f1_dual_rev1 firmware with OpenBLT bootloader, including macOS support.

## Prerequisites

- **ARM GCC toolchain** – GCC 11.3 recommended for reproducible builds (matches CI). See `tools/gcc-11.3/README.md` for setup.
- **Python 3** – Required on macOS for composite image assembly and checksum fix.
- **Make** – Standard build system.

### Optional (Linux / Windows)

- **hex2dfu** – Creates DFU files for USB flashing. Bundled in `ext/encedo_hex2dfu/` (Linux x86-64 only).
- **srecord** – `srec_cat` for Intel HEX generation. On Linux: `apt install srecord`; on macOS: `brew install srecord`.

On macOS, Python fallbacks replace hex2dfu and srec_cat when they are unavailable.

## Build Steps

### 1. Set up the toolchain (recommended)

```bash
export PATH="$(pwd)/tools/gcc-11.3/xpack-arm-none-eabi-gcc-11.3.1-1.1/bin:$PATH"
```

Or from the repo root:

```bash
PATH="$(pwd)/tools/gcc-11.3/xpack-arm-none-eabi-gcc-11.3.1-1.1/bin:$PATH" firmware/boards/f1_dual_rev1/build_wideband.sh
```

### 2. Run the build

From the board directory:

```bash
cd firmware/boards/f1_dual_rev1
./build_wideband.sh
```

Or with explicit env vars:

```bash
cd firmware/boards/f1_dual_rev1
BOARD=f1_dual_rev1 USE_OPENBLT=yes bash ../build_f1_board.sh
```

### 3. Output artifacts

Artifacts are written to `firmware/deliver/f1_dual_rev1/`:

| File | Description |
|------|-------------|
| `wideband.bin` | Composite bootloader + app for raw flashing |
| `wideband.hex` | Intel HEX for flashing |
| `wideband_update.srec` | S-record for CAN update |
| `openblt.bin` | Bootloader only |
| `openblt.elf` | Bootloader debug symbols |

## macOS Notes

- **hex2dfu** is a Linux x86-64 binary and does not run natively. The build uses a Python fallback to create the composite image.
- **DFU files** (`wideband.dfu`, `wideband_update.dfu`) are not produced on macOS. Use `wideband.bin` for raw flashing instead.
- **OpenBLT checksum** – A post-build step patches the vector table checksum at 0x1C for GCC 11.3 compatibility. This runs automatically on macOS.

## Verifying the build

Check the OpenBLT checksum:

```bash
cd firmware
python3 -c "
import struct
with open('deliver/f1_dual_rev1/wideband.bin', 'rb') as f:
    f.seek(8192)  # app start
    words = [struct.unpack('<I', f.read(4))[0] for _ in range(8)]
    s = sum(words[:7]) & 0xFFFFFFFF
    expected_cs = (0xFFFFFFFF & (~s + 1))
    verify = (s + words[7]) & 0xFFFFFFFF
    print('Checksum OK' if verify == 0 else f'Checksum FAIL: {hex(verify)}')
"
```

Expected output: `Checksum OK`
