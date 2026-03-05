# Midori

USB MIDI host firmware for ESP32-S3. Connect USB MIDI devices and send/receive MIDI messages.

## Features

- **USB MIDI Host** - Connect USB MIDI devices (synthesizers, keyboards, etc.) directly
- **Hot-plug Support** - Automatic detection of device connection/disconnection
- **M5Stack CoreS3 Support** - Touch screen UI operation (optional)
- **PicoRuby Integration** - Extension via Ruby scripts (in development)

## Supported Hardware

- ESP32-S3 boards (USB Host capability required)
- M5Stack CoreS3 (for UI features)

## Tested MIDI Devices

- Roland J-6

## Building

### Requirements

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/) v5.x

### Instructions

```bash
# Set up ESP-IDF environment
source ~/esp-idf/export.sh

# Build
idf.py build

# Flash to board & start serial monitor
idf.py flash monitor
```

### Board Selection

Use `idf.py menuconfig` to select your target board under `USB MIDI Host Configuration`.

## Architecture

Operates using an action-based state machine:

```
Device Connect → Get Info → Detect MIDI → Setup Endpoints → MIDI Communication
```

See [CLAUDE.md](CLAUDE.md) for details.

## License

MIT License
