#!/bin/bash
# Board switching script for USB MIDI Host project
# Usage: ./switch_board.sh [freenove|m5stack]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [ -z "$1" ]; then
    echo "Usage: $0 [freenove|m5stack]"
    echo ""
    echo "Boards:"
    echo "  freenove - Freenove ESP32-S3 (Octal PSRAM, SD card disabled)"
    echo "  m5stack  - M5Stack CoreS3 SE (Quad PSRAM, SD card via SPI)"
    exit 1
fi

BOARD="$1"

case "$BOARD" in
    freenove)
        echo "Switching to Freenove ESP32-S3..."
        DEFAULTS_FILE="sdkconfig.defaults.freenove"
        ;;
    m5stack)
        echo "Switching to M5Stack CoreS3 SE..."
        DEFAULTS_FILE="sdkconfig.defaults.m5stack"
        ;;
    *)
        echo "Error: Unknown board '$BOARD'"
        echo "Valid options: freenove, m5stack"
        exit 1
        ;;
esac

if [ ! -f "$DEFAULTS_FILE" ]; then
    echo "Error: $DEFAULTS_FILE not found"
    exit 1
fi

# Copy board-specific defaults
cp "$DEFAULTS_FILE" sdkconfig.defaults
echo "Applied $DEFAULTS_FILE"

# Remove sdkconfig and build directory
rm -f sdkconfig
rm -rf build
echo "Cleaned build artifacts"

echo ""
echo "Done! Now run:"
echo "  source ~/esp-idf/export.sh"
echo "  idf.py set-target esp32s3"
echo "  idf.py build flash monitor"
