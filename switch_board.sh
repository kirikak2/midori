#!/bin/bash
# Board switching script for USB MIDI Host project
# Usage: ./switch_board.sh [freenove|m5stack]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [ -z "$1" ]; then
    echo "Usage: $0 [freenove|m5stack|m5stack_with_usbserial|m5stack_tab5]"
    echo ""
    echo "Boards:"
    echo "  freenove              - Freenove ESP32-S3 (Octal PSRAM, SD card disabled)"
    echo "  m5stack               - M5Stack CoreS3 SE (Quad PSRAM, USB-MIDI host)"
    echo "  m5stack_with_usbserial - M5Stack CoreS3 SE (Quad PSRAM, USB serial debug)"
    echo "  m5stack_tab5          - M5Stack Tab5 (ESP32-P4, 1280x720, USB-A host)"
    exit 1
fi

BOARD="$1"
TARGET="esp32s3"  # Default target

case "$BOARD" in
    freenove)
        echo "Switching to Freenove ESP32-S3..."
        DEFAULTS_FILE="sdkconfig.defaults.freenove"
        ;;
    m5stack)
        echo "Switching to M5Stack CoreS3 SE (USB-MIDI host)..."
        DEFAULTS_FILE="sdkconfig.defaults.m5stack"
        ;;
    m5stack_with_usbserial)
        echo "Switching to M5Stack CoreS3 SE (USB serial debug)..."
        DEFAULTS_FILE="sdkconfig.defaults.m5stack_with_usbserial"
        ;;
    m5stack_tab5)
        echo "Switching to M5Stack Tab5 (ESP32-P4)..."
        DEFAULTS_FILE="sdkconfig.defaults.m5stack_tab5"
        TARGET="esp32p4"
        ;;
    *)
        echo "Error: Unknown board '$BOARD'"
        echo "Valid options: freenove, m5stack, m5stack_with_usbserial, m5stack_tab5"
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
echo "  idf.py set-target $TARGET"
echo "  idf.py build flash monitor"
