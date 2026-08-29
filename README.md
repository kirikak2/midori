# Midori

**MIDI on Ruby Interpreter** — Orchestrate connected MIDI devices using PicoRuby scripts. Runs on ESP32-S3 / ESP32-P4 with USB MIDI host and USB MIDI device capabilities.

## Features

- **USB MIDI Host** — Connect USB MIDI devices (synthesizers, keyboards, etc.) directly
- **USB MIDI Device** — In `midi_device` mode the board itself acts as a USB MIDI device to a host PC (TinyUSB CDC + MIDI composite; appears as `Midori <board>`)
- **Hot-plug Support** — Automatic detection and recovery on device connection/disconnection
- **PicoRuby Scripting** — Program MIDI sequences with Ruby scripts
- **BPM Loop** — `MIDI.bpm_loop` for BPM-synced loops with automatic MIDI Clock output
- **External Clock Sync** — Automatically follow external MIDI Clock BPM
- **MML Support** — Describe melodies and rhythms using Music Macro Language
- **Script Hot-swap** — Dynamically switch Ruby scripts without rebooting ESP32 (FreeRTOS Supervisor architecture)
- **Touch Screen UI** — 9-screen UI on CoreS3 / Tab5 / CrowPanel (BPM display, pads, MIDI info, log, script selector, settings, Tombola, knobs, XY pad)
- **SAM2695 Synthesizer** — Integration with onboard MIDI sound module

## Supported Hardware

| Board | Notes |
|-------|-------|
| M5Stack CoreS3 | Touch screen UI available |
| M5Stack Tab5 (ESP32-P4) | Touch screen UI; USB-A = MIDI host, USB-C = MIDI device |
| Elecrow CrowPanel Advanced 7inch (ESP32-P4) | Touch screen UI (1024x600); flashing and console over its own UART bridge — see [docs/CROWPANEL.md](docs/CROWPANEL.md) |
| Freenove ESP32-S3 | Script operation via serial console |

See [docs/MIDI_DEVICES.md](docs/MIDI_DEVICES.md) for per-board MIDI device availability and the `MIDIDevices` API (`sam2695` / `usb_midi_host` / `usb_midi_device`).

## Tested MIDI Devices (USB)

The following devices have been confirmed to work as USB MIDI hosts. MIDI-DIN is universally supported and not listed here.

- Roland J-6
- Teenage Engineering OP-1 field
- ROLI Seaboard BLOCKS
- ROLI Lightpad BLOCKS
- ROLI LUMI keys
- Novation Launch Control XL mk2

## Building

### Requirements

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/) v5.x

### Board Selection

Run the switch script for your target board, optionally followed by the USB
port mode:

```bash
./switch_board.sh <board> [host|serial|midi_device]

./switch_board.sh m5stack                  # M5Stack CoreS3, USB-MIDI host (default)
./switch_board.sh m5stack serial           # ... USB-Serial/JTAG console (for develop)
./switch_board.sh m5stack midi_device      # ... USB-MIDI device (TinyUSB CDC + MIDI)
./switch_board.sh m5stack_tab5             # M5Stack Tab5 (ESP32-P4), USB-C as USB-MIDI device (default)
./switch_board.sh m5stack_tab5 serial      # ... USB-C as USB-Serial/JTAG console
./switch_board.sh crowpanel                # Elecrow CrowPanel Advanced 7inch (ESP32-P4), USB-MIDI host (default)
./switch_board.sh crowpanel midi_device    # ... "USB 2.0" port as a USB-MIDI device
./switch_board.sh freenove                 # Freenove ESP32-S3 (for develop)
./switch_board.sh freenove midi_device     # ... as a USB-MIDI device
```

USB port modes:

| Mode | USB port role | Console | USB-MIDI host |
|------|---------------|---------|---------------|
| `host` | USB-OTG host | UART | yes |
| `serial` | USB-Serial/JTAG (flash + JTAG) | USB | ESP32-S3: no / Tab5: USB-A |
| `midi_device` | TinyUSB CDC + MIDI device | USB CDC | ESP32-S3: no / Tab5: USB-A |

Freenove and CoreS3 have a single USB connector wired to one USB PHY, so the
host and device roles are mutually exclusive there. On the Tab5 the mode only
selects what the USB-C port does — USB-A is always a USB-MIDI host. The
CrowPanel has a second USB-C carrying a CH343 UART bridge, which is always what
`idf.py flash` talks to, so `serial` is not offered there (its USB-Serial/JTAG
reaches no connector).

> **Note**: in `midi_device` mode USB-Serial/JTAG is disconnected from the
> connector, so `idf.py flash` requires download mode (hold BOOT, tap RESET)
> and hardware JTAG debugging is unavailable. `idf.py monitor` still works:
> the console is redirected to the TinyUSB CDC interface. On the CrowPanel
> flashing is unaffected — it never used USB-Serial/JTAG to begin with.

### Instructions

```bash
# Set up ESP-IDF environment
source ~/esp-idf/export.sh

# Build (fullclean recommended to avoid stale cache)
idf.py fullclean build

# Flash to board
idf.py flash

# If you use develop board, you can start serial monitor.
idf.py monitor
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
