# Midori

**MIDI on Ruby Interpreter** — a MIDI hub and instrument you program in Ruby.
Midori runs PicoRuby on ESP32-S3 / ESP32-P4 boards to connect MIDI gear over
USB (host and device) and UART, route and transform what flows between them,
and play it from a touch screen or the built-in AMY synthesizer. Scripts live
on the SD card and are swapped live, without rebooting.

```ruby
require 'midi'
require 'ui'

dev = MIDI::Device.new(MIDIDevices.usb_midi_host || MIDIDevices.sam2695)

UI.pad(1, label: "Kick")  { dev.trigger(36, 127, duration: 100) }
UI.knob(1, label: "Cutoff", value: 64) { |v| dev.control_change(74, v.to_i) }

MIDI.bpm_loop(120, output: dev) do
  UI.process
end
```

## Features

### Connect

- **USB MIDI Host** — Plug synthesizers, keyboards and controllers straight into the board, with hot-plug detection and recovery
- **USB MIDI Device** — In `midi_device` mode the board itself is a USB MIDI device to a computer (TinyUSB CDC + MIDI composite; appears as `Midori <board>`)
- **UART MIDI / SAM2695** — Serial MIDI out to DIN gear or the onboard SAM2695 General MIDI sound module
- **MIDI routing** — `MIDI.route(from, to, channel:)` forwards input to any output in C, without going through Ruby, so a keyboard can drive a synth with no added latency

### Script

- **PicoRuby scripting** — Notes, CCs, program change, pitch bend, aftertouch, SysEx, MIDI input event handlers
- **Non-blocking notes** — `device.trigger(note, velocity, duration:)` returns immediately; a C-side scheduler sends the note-off, so multi-touch playing stays in time
- **BPM loop & clock** — `MIDI.bpm_loop` runs BPM-synced loops with MIDI Clock output, or follows an external MIDI Clock
- **MML** — Write melodies and rhythms in Music Macro Language
- **Live script switching** — Load, stop and swap scripts from the touch screen or the console, without rebooting (FreeRTOS supervisor task)
- **irb** — An interactive Ruby prompt on the serial / USB CDC console
- **Browser console** — With TinyUSB CDC enabled (`midi_device` mode), connect to the Midori console from [picoruby.org/terminal](https://picoruby.org/terminal) over the USB serial port to load scripts and upload / download SD card files. Nothing to install.

### Play

- **Touch screen UI** (CoreS3 / Tab5 / CrowPanel) — BPM, MIDI info, log, script selector and settings screens, plus playing surfaces scripts can drive:
  - **Pads** — One-shot buttons (`UI.pad`)
  - **Knobs** — Banks of rotary knobs for continuous parameters such as CCs (`UI.knob`, [docs](docs/KNOBS.md))
  - **XY Pad** — Up to five fingers, each playing pitch (snapped to a scale, with glide) on X and a CC on Y (`UI::XYPad`, [docs](docs/XYPAD.md))
  - **Tombola** — A physics sequencer: balls bounce inside a rotating polygon and play a note on every wall hit (`UI::Tombola`, [docs](docs/TOMBOLA.md))
- **AMY synthesizer** (Tab5 / CrowPanel) — The [AMY](https://github.com/shorepine/amy) engine plays through the built-in speaker, as an ordinary MIDI output: `AMY::FM` (DX7 presets) and `AMY::Synth` (subtractive, Juno-style), see [docs](docs/AMY_SYNTH.md)
- **Rotary encoders** — DFRobot SEN0502 I2C encoders as physical knobs

## The picoruby-midi Family

Midori's MIDI features are built as standalone PicoRuby mrbgems, each in its
own repository so you can use them in other PicoRuby projects. They are
included here as submodules under [mrbgems/](mrbgems/). At the center is
**picoruby-midi**, the protocol layer. It doesn't depend on any particular
transport: every transport gem below plugs into its transport registry, so
`MIDI::Device`, `MIDI::Input`, the note scheduler, the clock and `MIDI.route`
work the same with any of them.

| Gem | Role |
|-----|------|
| [picoruby-midi](https://github.com/kirikak2/picoruby-midi) | Protocol layer: parser, `MIDI::Device` / `MIDI::Input`, note scheduler, clock and `bpm_loop`, routing, transport registry |
| [picoruby-midi-mml](https://github.com/kirikak2/picoruby-midi-mml) | MML (Music Macro Language) parser and player |
| [picoruby-usb_midi_host](https://github.com/kirikak2/picoruby-usb_midi_host) | Transport: USB MIDI host |
| [picoruby-usb_midi_device](https://github.com/kirikak2/picoruby-usb_midi_device) | Transport: USB MIDI device (TinyUSB, optionally with CDC) |
| [picoruby-uart_midi](https://github.com/kirikak2/picoruby-uart_midi) | Transport: UART / 5-pin DIN MIDI |
| [picoruby-sam2695](https://github.com/kirikak2/picoruby-sam2695) | Transport: SAM2695 GM sound module, a thin layer over uart_midi |
| [picoruby-amy](https://github.com/kirikak2/picoruby-amy) | Transport: [AMY](https://github.com/shorepine/amy) synth over I2S, with `AMY::FM` / `AMY::Synth` |

`picoruby-ui` (the M5Stack touch UI) lives in this repository, because it
is specific to Midori. The gems currently have an ESP32 port only.

A new transport registers itself with `MIDI_transport_register()` (see
`midi_transport.h` in picoruby-midi) and then works with everything above,
with no changes to picoruby-midi.

## Supported Hardware

| Board | Notes |
|-------|-------|
| M5Stack CoreS3 | Touch screen UI available |
| M5Stack Tab5 (ESP32-P4) | Touch screen UI; USB-A = MIDI host, USB-C = MIDI device |
| Elecrow CrowPanel Advanced 7inch (ESP32-P4) | Touch screen UI (1024x600); flashing and console over its own UART bridge — see [docs/CROWPANEL.md](docs/CROWPANEL.md) |
| Freenove ESP32-S3 | Script operation via serial console |

See [docs/MIDI_DEVICES.md](docs/MIDI_DEVICES.md) for per-board MIDI device availability and the `MIDIDevices` API (`sam2695` / `usb_midi_host` / `usb_midi_device`).

## Tested MIDI Devices (USB)

The following devices have been confirmed to work when connected to Midori's USB MIDI host port. MIDI-DIN is universally supported and not listed here.

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

## Running Scripts

Put `.rb` files on the SD card (or upload them from the browser, see below),
then either pick one on the **Scripts** screen, or type on the console (UART
in `host` mode, USB CDC in `midi_device` mode):

```
> load /sd/app.rb    # run a script
> stop               # stop it and return to the UI
> irb                # interactive Ruby
> heap               # free heap
> help
```

### From the browser (picoruby.org/terminal)

When TinyUSB CDC is enabled (`midi_device` mode), the board's USB port is
also a USB serial port. The [PicoRuby web terminal](https://picoruby.org/terminal)
can connect to it through WebSerial (Chrome / Edge) and gives you the Midori
console in the browser. No serial tools or drivers are needed:

1. Build with `./switch_board.sh <board> midi_device`, flash, and plug the
   board's USB port into your computer
2. Open <https://picoruby.org/terminal> and connect to the `Midori <board>`
   serial port
3. At the `> ` prompt:
   - **Load scripts**: type the console commands above (`load /sd/app.rb`,
     `stop`, `irb`, ...)
   - **Upload / download files**: use the terminal's file editor to send a
     script to the SD card or fetch one back. This uses the same PicoModem
     protocol as R2P2, so the web terminal works unmodified.

File transfers only work while no script is running, at the `> ` prompt.
See [docs/PICOMODEM.md](docs/PICOMODEM.md) for the details.

### Choosing outputs

Scripts should get their MIDI outputs from `MIDIDevices` instead of
hard-coding pins, so the same script runs on every board:

| Method | Transport |
|--------|-----------|
| `MIDIDevices.usb_midi_host` | USB MIDI device plugged into the board |
| `MIDIDevices.usb_midi_device` | The computer the board is plugged into |
| `MIDIDevices.sam2695` | UART MIDI / SAM2695 |
| `MIDIDevices.amy` | Built-in AMY synth (Tab5 / CrowPanel) |

Each returns `nil` when the transport is not available on the current board
and USB mode. Scripts with long-running loops of their own should check
`ScriptManager.new.stop_requested?` and `break`, so they can be stopped
cleanly (`MIDI.bpm_loop` already does).

## PicoRuby API

### MIDI::Device

```ruby
dev = MIDI::Device.new(MIDIDevices.usb_midi_host)

dev.note_on(60, 100, channel: 0)
dev.note_off(60, channel: 0)
dev.trigger(36, 127, duration: 100)    # note-off is sent automatically
dev.control_change(74, 64)
dev.program_change(5)
dev.pitch_bend(2048)                   # -8192..8191, center 0
dev.send_sysex([0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7])
```

### MIDI input

```ruby
input = MIDI::Input.new(MIDI::Device.new(MIDIDevices.usb_midi_host))
input.on(:note_on)        { |e| UI.log("note #{e[:note]}") }
input.on(:control_change) { |e| UI.log("cc #{e[:cc]} = #{e[:value]}") }
```

### MIDI.route

```ruby
# Keyboard on the USB host port -> built-in synth, forwarded in C
MIDI.route(MIDIDevices.usb_midi_host, MIDIDevices.amy)
MIDI.route(MIDIDevices.usb_midi_host, MIDIDevices.sam2695, channel: 9)
MIDI.unroute_all
```

### MIDI.bpm_loop

```ruby
# Loop at 120 BPM with MIDI Clock output
MIDI.bpm_loop(120, output: device) do
  device.trigger(60, 100, duration: 100)
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

### Examples

Ready-to-run scripts are in [examples/](examples/):

| Script | What it does |
|--------|--------------|
| [pad.rb](examples/pad.rb) | Drum pads on the touch screen |
| [knobs.rb](examples/knobs.rb) | CC knob banks |
| [xypad.rb](examples/xypad.rb) | Multi-finger XY pad |
| [tombola.rb](examples/tombola.rb) | Bouncing-ball sequencer |
| [amy_fm.rb](examples/amy_fm.rb) / [amy_synth.rb](examples/amy_synth.rb) | Play the built-in synth from pads, XY pad, knobs and a USB keyboard |
| [bach_air.rb](examples/bach_air.rb) | MML playback |
| [midi_monitor.rb](examples/midi_monitor.rb) | Log every incoming MIDI message |
| [launch_control_xl.rb](examples/launch_control_xl.rb) / [seaboard_blocks.rb](examples/seaboard_blocks.rb) / [midi_to_lumi.rb](examples/midi_to_lumi.rb) | Device-specific controllers |
| [dfrobot_encoder_cc.rb](examples/dfrobot_encoder_cc.rb) | Hardware rotary encoders as CC knobs |

## Architecture

```
Core 1  Supervisor task ─── PicoRuby task (created / deleted per script)
                               └─ main_task.rb → SD card script / irb
        Console task ─────── line editing, load / stop / irb, PicoModem
        MIDI input task ──── parsing, MIDI.route forwarding
Core 0  Main task ────────── app_main loop: touch UI (M5GFX / LovyanGFX)
        USB host tasks ───── USB MIDI host driver
        AMY audio task ───── synth render + I2S (Tab5 / CrowPanel)
```

The MIDI stack is the [picoruby-midi family](#the-picoruby-midi-family) of
mrbgems. See [CLAUDE.md](CLAUDE.md) and [docs/](docs/)
for details.

## License

MIT License
