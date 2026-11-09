#!/bin/bash
# Build one release firmware image (board x USB mode) and package it.
#
# Usage: tools/release/build.sh <board> <usb-mode> [out-dir]
#
# Runs inside an ESP-IDF environment (export.sh sourced, or the espressif/idf
# container). Produces, in out-dir (default: dist/):
#
#   midori-<ver>-<board>-<mode>.bin   merged image, written at offset 0x0
#                                     (what the web installer and
#                                     "esptool write_flash 0x0" use)
#   midori-<ver>-<board>-<mode>.zip   bootloader / partition table / app with
#                                     flash_args, for flashing the parts
#
# The version is MIDORI_VERSION if set (CI passes the tag), otherwise what
# ESP-IDF derives from `git describe`. It ends up in the app description
# (esp_app_get_description()->version), shown on the Settings screen.

set -euo pipefail

BOARD="${1:?board}"
MODE="${2:?usb mode}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$(mkdir -p "${3:-$ROOT/dist}" && cd "${3:-$ROOT/dist}" && pwd)"

cd "$ROOT"

case "$BOARD" in
    m5stack_tab5|crowpanel) TARGET=esp32p4 ;;
    *)                      TARGET=esp32s3 ;;
esac

if [ -n "${MIDORI_VERSION:-}" ]; then
    # ESP-IDF reads version.txt when PROJECT_VER is not set in CMakeLists.
    printf '%s\n' "$MIDORI_VERSION" > version.txt
    VERSION="$MIDORI_VERSION"
else
    rm -f version.txt
    VERSION="$(git describe --tags --always --dirty 2>/dev/null || echo dev)"
fi

./switch_board.sh "$BOARD" "$MODE"
idf.py set-target "$TARGET"
idf.py build

NAME="midori-${VERSION}-${BOARD}-${MODE}"

# build/flash_args holds the flash settings and every part's offset (the
# bootloader is at 0x0 on the S3 and 0x2000 on the P4).
(cd build && python -m esptool --chip "$TARGET" merge_bin \
    -o "$OUT/$NAME.bin" @flash_args)

STAGE="$(mktemp -d)"
mkdir -p "$STAGE/$NAME/bootloader" "$STAGE/$NAME/partition_table"
cp build/bootloader/bootloader.bin "$STAGE/$NAME/bootloader/"
cp build/partition_table/partition-table.bin "$STAGE/$NAME/partition_table/"
cp build/midori.bin build/flash_args build/flasher_args.json "$STAGE/$NAME/"
cat > "$STAGE/$NAME/README.txt" <<EOF
MIDoRI ${VERSION} - ${BOARD} (${MODE})

Flash every part (from this directory):
  python -m esptool --chip ${TARGET} write_flash @flash_args

Or flash the merged image ${NAME}.bin from the release page:
  python -m esptool --chip ${TARGET} write_flash 0x0 ${NAME}.bin
EOF
(cd "$STAGE" && rm -f "$OUT/$NAME.zip" && python -m zipfile -c "$OUT/$NAME.zip" "$NAME")
rm -rf "$STAGE"

[ -n "${MIDORI_VERSION:-}" ] && rm -f version.txt

echo "Built $OUT/$NAME.bin"
