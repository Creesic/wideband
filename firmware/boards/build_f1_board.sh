#!/bin/bash

set -eo pipefail

if [ ! "$USE_OPENBLT" ]; then
  USE_OPENBLT=no
fi

if [ $USE_OPENBLT = "yes" ]; then
  cd openblt

  echo ""
  echo "Building bootloader"
  make clean
  make -j12 BOARD=${BOARD} || exit 1

  # back to board dir
  cd ..
fi

cd ../..

echo ""
echo "Build application"
# make clean
make -j12 BOARD=${BOARD} || exit 1

DELIVER_DIR=deliver/${BOARD}
mkdir -p ${DELIVER_DIR}
rm -f ${DELIVER_DIR}/*

if uname | grep "NT"; then
  HEX2DFU=./ext/encedo_hex2dfu/hex2dfu.exe
  SREC_CAT=srec_cat.exe
else
  HEX2DFU=./ext/encedo_hex2dfu/hex2dfu.bin
  chmod u+x $HEX2DFU
  SREC_CAT=srec_cat
fi

echo ""
echo "Creating deliveries:"

if [ $USE_OPENBLT = "yes" ]; then
  SKIP_SREC_CAT=0
  echo "Srec for CAN update"
  cp -v build/wideband.srec ${DELIVER_DIR}/wideband_update.srec

  echo ""
  echo "Invoking hex2dfu for incremental Wideband image (for DFU util)"
  $HEX2DFU -i build/wideband.hex -C 0x1C -o ${DELIVER_DIR}/wideband_update.dfu 2>/dev/null || \
    [ "$(uname)" = "Darwin" ] && echo "  (skipped on macOS - hex2dfu is Linux-only)"

  echo ""
  echo "Invoking hex2dfu for OpenBLT (for DFU util)"
  $HEX2DFU -i boards/${BOARD}/openblt/bin/openblt_${BOARD}.hex -o ${DELIVER_DIR}/openblt.dfu 2>/dev/null || \
    [ "$(uname)" = "Darwin" ] && echo "  (skipped on macOS - hex2dfu is Linux-only)"

  echo ""
  echo "OpenBLT bin (for DFU another util)"
  cp -v boards/${BOARD}/openblt/bin/openblt_${BOARD}.bin ${DELIVER_DIR}/openblt.bin

  echo ""
  echo "OpenBLT elf for debugging"
  cp -v boards/${BOARD}/openblt/bin/openblt_${BOARD}.elf ${DELIVER_DIR}/openblt.elf

  OPENBLT_HEX=boards/${BOARD}/openblt/bin/openblt_${BOARD}.hex
  OPENBLT_BIN=boards/${BOARD}/openblt/bin/openblt_${BOARD}.bin
  echo ""
  echo "Creating composite OpenBLT+Wideband image"
  if $HEX2DFU -i ${OPENBLT_HEX} -i build/wideband.hex -C 0x1C -o ${DELIVER_DIR}/wideband.dfu -b ${DELIVER_DIR}/wideband.bin 2>/dev/null; then
    echo "  (via hex2dfu)"
  elif [ "$(uname)" = "Darwin" ]; then
    echo "  (via Python fallback - hex2dfu is Linux-only)"
    python3 tools/build_composite_macos.py ${OPENBLT_BIN} build/wideband.bin ${DELIVER_DIR}/wideband.bin ${DELIVER_DIR}/wideband.hex || exit 1
    echo "  Note: wideband.dfu not created (hex2dfu required). Use wideband.bin for raw flashing."
    SKIP_SREC_CAT=1
  else
    echo "Error: hex2dfu failed and no fallback for this platform"
    exit 1
  fi
  if [ "$(uname)" = "Darwin" ]; then
    echo "Fixing OpenBLT vector table checksum (macOS/GCC 11.3)"
    python3 tools/fix_openblt_checksum.py ${DELIVER_DIR}/wideband.bin || exit 1
  fi
  if [ "${SKIP_SREC_CAT}" != "1" ]; then
    echo "Creating composite hex file"
    $SREC_CAT ${DELIVER_DIR}/wideband.bin -binary -offset 0x08000000 -o ${DELIVER_DIR}/wideband.hex -Intel
  fi
else
  echo "Bin for raw flashing"
  cp build/wideband.bin ${DELIVER_DIR}

  cp build/wideband.hex ${DELIVER_DIR}

  echo "elf for debugging"
  cp build/wideband.elf ${DELIVER_DIR}

  echo "Invoking hex2dfu for DFU file"
  $HEX2DFU -i build/wideband.hex -o ${DELIVER_DIR}/wideband.dfu
fi

echo ""
echo "${DELIVER_DIR} folder content:"
ls -l ${DELIVER_DIR}
