#!/bin/bash
# Board / USB mode switching script for the Midori project
# Usage: ./switch_board.sh [freenove|m5stack|m5stack_tab5|crowpanel] [host|serial|midi_device]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

usage() {
    cat <<'EOF'
Usage: ./switch_board.sh <board> [usb-mode]

Boards:
  freenove      - Freenove ESP32-S3 (Octal PSRAM)
  m5stack       - M5Stack CoreS3 SE (Quad PSRAM)
  m5stack_tab5  - M5Stack Tab5 (ESP32-P4, 1280x720, USB-A host)
  crowpanel     - Elecrow CrowPanel Advanced 7" (ESP32-P4, 1024x600)

USB modes (what the USB device-capable port is used for):
  host          - USB-MIDI host; console on UART            [default: freenove, m5stack, crowpanel]
  serial        - USB-Serial/JTAG console (flash + JTAG)
  midi_device   - USB-MIDI device via TinyUSB (CDC + MIDI)  [default: m5stack_tab5]

Notes:
  * Freenove / CoreS3 have a single USB connector and one USB PHY, so
    'serial' and 'midi_device' disable the USB-MIDI host in that build.
  * Tab5's USB-A port is always a USB-MIDI host; the mode only selects what
    the USB-C port does.
  * In 'midi_device' mode USB-Serial/JTAG is disconnected: flashing needs
    download mode (hold BOOT, tap RESET) and JTAG debugging is unavailable.
  * CrowPanel flashes and logs over its CH343 UART bridge ("UART" USB-C) in
    every mode, so the console never moves to USB there. 'serial' is not
    available: the P4's USB-Serial/JTAG is not brought out to a connector.

Examples:
  ./switch_board.sh m5stack                 # CoreS3 as USB-MIDI host
  ./switch_board.sh m5stack serial          # CoreS3 with USB serial console
  ./switch_board.sh m5stack midi_device     # CoreS3 as a USB-MIDI instrument
  ./switch_board.sh freenove midi_device    # Freenove as a USB-MIDI instrument
  ./switch_board.sh m5stack_tab5 serial     # Tab5 with USB-C serial console
  ./switch_board.sh crowpanel               # CrowPanel as USB-MIDI host
  ./switch_board.sh crowpanel midi_device   # CrowPanel as a USB-MIDI instrument
EOF
}

if [ -z "$1" ]; then
    usage
    exit 1
fi

BOARD="$1"
MODE="$2"
TARGET="esp32s3"  # Default target

# Backwards-compatible alias for the old board name
if [ "$BOARD" = "m5stack_with_usbserial" ]; then
    echo "Note: 'm5stack_with_usbserial' is now 'm5stack serial'"
    BOARD="m5stack"
    MODE="${MODE:-serial}"
fi

case "$BOARD" in
    freenove)
        BOARD_LABEL="Freenove ESP32-S3"
        DEFAULT_MODE="host"
        ;;
    m5stack)
        BOARD_LABEL="M5Stack CoreS3 SE"
        DEFAULT_MODE="host"
        ;;
    m5stack_tab5)
        BOARD_LABEL="M5Stack Tab5 (ESP32-P4)"
        DEFAULT_MODE="midi_device"
        TARGET="esp32p4"
        ;;
    crowpanel)
        BOARD_LABEL="Elecrow CrowPanel Advanced 7in (ESP32-P4)"
        DEFAULT_MODE="host"
        TARGET="esp32p4"
        ;;
    *)
        echo "Error: Unknown board '$BOARD'"
        echo ""
        usage
        exit 1
        ;;
esac

MODE="${MODE:-$DEFAULT_MODE}"

case "$MODE" in
    host)        MODE_LABEL="USB-MIDI host" ;;
    serial)      MODE_LABEL="USB-Serial/JTAG console" ;;
    midi_device) MODE_LABEL="USB-MIDI device (TinyUSB CDC + MIDI)" ;;
    *)
        echo "Error: Unknown USB mode '$MODE'"
        echo ""
        usage
        exit 1
        ;;
esac

BOARD_FILE="sdkconfig.defaults.$BOARD"
MODE_FILE="sdkconfig.defaults.usbmode.$MODE"

for f in "$BOARD_FILE" "$MODE_FILE"; do
    if [ ! -f "$f" ]; then
        echo "Error: $f not found"
        exit 1
    fi
done

if [ "$BOARD" = "m5stack_tab5" ] && [ "$MODE" = "host" ]; then
    echo "Note: Tab5 always has the USB-A host; 'host' mode just leaves USB-C unused."
fi

# The CrowPanel's USB-Serial/JTAG never reaches a connector, so a 'serial'
# build would come up with its console wired to nothing: no boot log, no
# prompt, no way to tell it apart from a board that failed to start.
if [ "$BOARD" = "crowpanel" ] && [ "$MODE" = "serial" ]; then
    echo "Error: 'serial' is not available on the CrowPanel."
    echo "       Its USB-Serial/JTAG is not brought out to a connector; the"
    echo "       console is the CH343 UART bridge on the 'UART' USB-C port,"
    echo "       which works in both 'host' and 'midi_device' mode."
    exit 1
fi

echo "Switching to $BOARD_LABEL - $MODE_LABEL..."

# Detect a CPU architecture change (xtensa <-> riscv). PicoRuby builds into a
# single arch-agnostic directory (picoruby/build/esp32) and rake does not
# notice that the toolchain changed, so those objects must go when the target
# does. A mode switch on the same board keeps them (they do not depend on
# sdkconfig).
PREV_TARGET=""
if [ -f sdkconfig.defaults ]; then
    PREV_TARGET="$(sed -n 's/^CONFIG_IDF_TARGET="\(.*\)"/\1/p' sdkconfig.defaults | tail -1)"
fi

# Compose board defaults + USB mode defaults
{
    cat "$BOARD_FILE"
    echo ""
    cat "$MODE_FILE"
} > sdkconfig.defaults
echo "Applied $BOARD_FILE + $MODE_FILE"

# Remove sdkconfig and build directory
rm -f sdkconfig
rm -rf build
echo "Cleaned build artifacts"

if [ -n "$PREV_TARGET" ] && [ "$PREV_TARGET" != "$TARGET" ]; then
    rm -rf components/picoruby-esp32/picoruby/build
    echo "Cleaned PicoRuby build artifacts ($PREV_TARGET -> $TARGET)"
fi

echo ""
echo "Done! Now run:"
echo "  source ~/esp-idf/export.sh"
echo "  idf.py set-target $TARGET"
echo "  idf.py build flash monitor"
if [ "$MODE" = "midi_device" ]; then
    echo ""
    if [ "$BOARD" = "crowpanel" ]; then
        echo "Note: flashing and the console stay on the CH343 UART bridge"
        echo "  ('UART' USB-C), so nothing changes about how you flash."
    else
        echo "Reminder: USB-MIDI device mode disconnects USB-Serial/JTAG."
        echo "  Enter download mode before flashing (hold BOOT, tap RESET)."
    fi
fi
