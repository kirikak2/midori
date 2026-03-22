# Midori

**MIDI on Ruby Interpreter** — Orchestrate connected MIDI devices using PicoRuby scripts. Runs on ESP32-S3 with USB MIDI host capabilities.

## Features

- **USB MIDI Host** — Connect USB MIDI devices (synthesizers, keyboards, etc.) directly
- **Hot-plug Support** — Automatic detection and recovery on device connection/disconnection
- **PicoRuby Scripting** — Program MIDI sequences with Ruby scripts
- **BPM Loop** — `MIDI.bpm_loop` for BPM-synced loops with automatic MIDI Clock output
- **External Clock Sync** — Automatically follow external MIDI Clock BPM
- **MML Support** — Describe melodies and rhythms using Music Macro Language
- **Script Hot-swap** — Dynamically switch Ruby scripts without rebooting ESP32 (FreeRTOS Supervisor architecture)
- **M5Stack CoreS3 UI** — Touch screen 6-screen UI (BPM display, pad, MIDI info, log, script selector, settings)
- **SAM2695 Synthesizer** — Integration with onboard MIDI sound module

## Supported Hardware

| Board | Notes |
|-------|-------|
| M5Stack CoreS3 | Touch screen UI available |
| Freenove ESP32-S3 | Script operation via serial console |

## Tested MIDI Devices

- Roland J-6
- Teenage Engineering OP-1 field

## Building

### Requirements

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/) v5.x

### Board Selection

Run the switch script for your target board:

```bash
./switch_board m5stack    # M5Stack CoreS3
./switch_board m5stack_with_usbserial # M5Stack CoreS3 using USB-Serial mode
./switch_board freenove   # Freenove ESP32-S3
```

### Instructions

```bash
# Set up ESP-IDF environment
source ~/esp-idf/export.sh

# Build (fullclean recommended to avoid stale cache)
idf.py fullclean build

# Flash to board and start serial monitor
idf.py flash monitor
```

> **Development note**: Stale build artifacts can cause inconsistencies. Use `idf.py fullclean build` to be safe.
> Also consider deleting the `build/` directory under `components/picoruby-esp32/picoruby/` before building.

## PicoRuby API

### MIDI.bpm_loop

```ruby
# Loop at 120 BPM with MIDI Clock output
MIDI.bpm_loop(120, output: device) do
  device.note_on(0, 60, 100)
  MIDI.sleep_ms(100)
  device.note_off(0, 60)
end

# Sync to external MIDI Clock
MIDI.bpm_loop(120, output: device, sync: true, input: input) do
  # BPM follows external clock automatically
end
```

### MML (Music Macro Language)

```ruby
seq = MIDI::MML::Sequence.new("l8 cdefgab>c", channel: 0)
player = MIDI::MML::Player.new(device, seq, loop: true)

MIDI.bpm_loop(120, output: device) do |clock|
  player.tick(clock)
end
```

## Architecture

```
Supervisor Task (Core 1) --- PicoRuby Task (dynamically created/deleted)
                                  └─ main_task.rb
USB Host Task (Core 0) ─────────────────────────────────────────────────
```

See [CLAUDE.md](CLAUDE.md) and [docs/](docs/) for details.

## License

MIT License
