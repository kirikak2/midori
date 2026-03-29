#!/bin/bash
# Play MIDI file on PC for timing verification

MIDI_FILE="${1:-bach_air_test.mid}"

if [ ! -f "$MIDI_FILE" ]; then
    echo "Error: MIDI file not found: $MIDI_FILE"
    echo "Usage: $0 <midi_file>"
    exit 1
fi

echo "Playing: $MIDI_FILE"
echo "Press Ctrl+C to stop"
echo ""

# Try different players in order of preference
if command -v timidity &> /dev/null; then
    echo "Using TiMidity++"
    timidity "$MIDI_FILE"
elif command -v fluidsynth &> /dev/null; then
    # Find soundfont
    SOUNDFONT=""
    for sf in /usr/share/sounds/sf2/FluidR3_GM.sf2 \
              /usr/share/soundfonts/FluidR3_GM.sf2 \
              /usr/share/sounds/sf2/default.sf2; do
        if [ -f "$sf" ]; then
            SOUNDFONT="$sf"
            break
        fi
    done

    if [ -z "$SOUNDFONT" ]; then
        echo "Error: No soundfont found"
        echo "Please install: sudo apt install fluid-soundfont-gm"
        exit 1
    fi

    echo "Using FluidSynth with soundfont: $SOUNDFONT"
    fluidsynth -a alsa -m alsa_seq -l -i "$SOUNDFONT" "$MIDI_FILE"
elif command -v pmidi &> /dev/null; then
    echo "Using pmidi"
    pmidi -p 128:0 "$MIDI_FILE"
else
    echo "Error: No MIDI player found"
    echo "Please install one of:"
    echo "  sudo apt install timidity"
    echo "  sudo apt install fluidsynth fluid-soundfont-gm"
    exit 1
fi
