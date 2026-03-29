#!/usr/bin/env ruby
# frozen_string_literal: true

# MIDI to MML converter
# Converts MIDI file to MML (Music Macro Language) format
# Follows the MML specification in docs/MML_DESIGN.md

require 'midilib'

# Note number to MML note name conversion (C4 = 60)
# Using lowercase as per MML_DESIGN.md
NOTE_NAMES = %w[c c+ d d+ e f f+ g g+ a a+ b].freeze

# Convert MIDI note number to MML note name and octave
def note_to_mml(note_num)
  octave = (note_num / 12) - 1
  note = NOTE_NAMES[note_num % 12]
  [note, octave]
end

# Quantize MIDI ticks to MML resolution
# MML uses 24 PPQ, so 1 MML clock = ticks_per_beat / 24
# For 480 PPQ: 1 clock = 20 ticks
def quantize_to_mml_clocks(ticks, ticks_per_beat)
  clock_unit = ticks_per_beat / 24.0
  clocks = (ticks / clock_unit).round
  (clocks * clock_unit).round
end

# Ensure minimum duration (32nd note = 3 clocks = 60 ticks for 480 PPQ)
def ensure_minimum_duration(ticks, ticks_per_beat)
  min_ticks = (ticks_per_beat / 24.0 * 3).round  # 3 clocks = 32nd note
  [ticks, min_ticks].max
end

# Convert MIDI ticks to MML note length
#
# MML lengths with dotted notes support:
# - 1: whole note (全音符) = 4 beats
# - 2.: dotted half note = 3 beats
# - 2: half note (二分音符) = 2 beats
# - 4.: dotted quarter note = 1.5 beats
# - 4: quarter note (四分音符) = 1 beat
# - 8.: dotted eighth note = 0.75 beats
# - 8: eighth note (八分音符) = 0.5 beat
# - 16.: dotted sixteenth note = 0.375 beats
# - 16: sixteenth note (十六分音符) = 0.25 beat
# - 32: thirty-second note (三十二分音符) = 0.125 beat
def ticks_to_mml_length(ticks, ticks_per_beat)
  beats = ticks.to_f / ticks_per_beat

  # Common note lengths: [mml_string, beat_value]
  note_lengths = [
    ['1', 4.0],
    ['2.', 3.0],
    ['2', 2.0],
    ['4.', 1.5],
    ['4', 1.0],
    ['8.', 0.75],
    ['8', 0.5],
    ['16.', 0.375],
    ['16', 0.25],
    ['32', 0.125]
  ]

  # Find closest match
  best_match = '4'  # Default to quarter note
  min_diff = Float::INFINITY

  note_lengths.each do |mml_len, beat_val|
    diff = (beats - beat_val).abs
    if diff < min_diff
      min_diff = diff
      best_match = mml_len
    end
  end

  best_match
end

# Convert MIDI ticks to MML with ties for accurate durations
# Returns a string like "1&8&32" for complex durations
# Uses MML clock units (24 clocks per beat) for accurate representation
def ticks_to_mml_with_ties(ticks, ticks_per_beat)
  # Convert to MML clocks (24 clocks per beat)
  clock_unit = ticks_per_beat / 24.0
  clocks = (ticks / clock_unit).round

  return '32' if clocks <= 0  # Minimum duration

  # Available note lengths in descending order: [mml_string, clock_value]
  note_lengths = [
    ['1', 96],    # whole note
    ['2.', 72],   # dotted half
    ['2', 48],    # half note
    ['4.', 36],   # dotted quarter
    ['4', 24],    # quarter note
    ['8.', 18],   # dotted eighth
    ['8', 12],    # eighth note
    ['16.', 9],   # dotted sixteenth
    ['16', 6],    # sixteenth note
    ['32', 3]     # thirty-second note (minimum)
  ]

  result = []
  remaining = clocks

  # Greedy algorithm: use largest note lengths first
  note_lengths.each do |mml_len, clock_val|
    while remaining >= clock_val
      result << mml_len
      remaining -= clock_val
      break if remaining == 0
    end
  end

  # If there's still remaining (shouldn't happen with proper quantization),
  # add a 32nd note to cover it
  if remaining > 0
    result << '32'
  end

  # Join with tie symbol
  result.join('&')
end

# Convert tempo (microseconds per quarter note) to BPM
def tempo_to_bpm(tempo)
  (60_000_000.0 / tempo).round
end

# Convert a single track to MML string
def convert_track_to_mml(track, ticks_per_beat, track_name, channel, performance_tempo = 500_000)
  # Collect note events with timing
  notes = []
  current_time = 0
  tempo = performance_tempo  # Use conductor track tempo

  # Build a map of note_on to note_off events
  active_notes = {}  # note_number -> {start_time:, velocity:}

  track.each do |event|
    current_time += event.delta_time

    case event
    # Skip tempo events - use conductor track tempo instead
    # when MIDI::Tempo
    #   tempo = event.tempo
    when MIDI::NoteOn
      if event.velocity > 0
        # Start of note - quantize to MML resolution
        quantized_start = quantize_to_mml_clocks(current_time, ticks_per_beat)
        active_notes[event.note] = {
          start_time: quantized_start,
          raw_start: current_time,
          velocity: event.velocity
        }
      else
        # Note on with velocity 0 is equivalent to note off
        if active_notes.key?(event.note)
          start_info = active_notes[event.note]
          # Quantize end time and ensure minimum duration
          quantized_end = quantize_to_mml_clocks(current_time, ticks_per_beat)
          duration_ticks = quantized_end - start_info[:start_time]
          duration_ticks = ensure_minimum_duration(duration_ticks, ticks_per_beat)
          quantized_end = start_info[:start_time] + duration_ticks

          note_name, octave = note_to_mml(event.note)
          length = ticks_to_mml_length(duration_ticks, ticks_per_beat)

          notes << {
            time: start_info[:start_time],
            end_time: quantized_end,
            note: note_name,
            octave: octave,
            length: length,
            velocity: start_info[:velocity]
          }

          active_notes.delete(event.note)
        end
      end
    when MIDI::NoteOff
      # End of note
      if active_notes.key?(event.note)
        start_info = active_notes[event.note]
        # Quantize end time and ensure minimum duration
        quantized_end = quantize_to_mml_clocks(current_time, ticks_per_beat)
        duration_ticks = quantized_end - start_info[:start_time]
        duration_ticks = ensure_minimum_duration(duration_ticks, ticks_per_beat)
        quantized_end = start_info[:start_time] + duration_ticks

        note_name, octave = note_to_mml(event.note)
        length = ticks_to_mml_length(duration_ticks, ticks_per_beat)

        notes << {
          time: start_info[:start_time],
          end_time: quantized_end,
          note: note_name,
          octave: octave,
          length: length,
          velocity: start_info[:velocity]
        }

        active_notes.delete(event.note)
      end
    end
  end

  # Generate MML string
  return nil if notes.empty?

  bpm = tempo_to_bpm(tempo)

  # Sort notes by start time
  notes.sort_by! { |n| n[:time] }

  # Build MML sequence WITH STRICT TIMING
  # Strategy: Each note starts at its exact quantized position
  # Use note's duration that ends exactly when next note starts
  mml_tokens = []
  current_octave = nil
  current_clock = 0  # Track current position in MML clocks

  # MML clock unit
  clock_unit = ticks_per_beat / 24.0

  # Pre-calculate all note start positions in clocks
  note_start_clocks_array = notes.map { |n| (n[:time] / clock_unit).round }

  notes.each_with_index do |note, i|
    note_start_clocks = note_start_clocks_array[i]

    # Calculate gap in clocks from current position to this note's start
    gap_clocks = note_start_clocks - current_clock

    if gap_clocks > 0
      # Fill gap with rest - use exact clock-based calculation
      rest_ticks = (gap_clocks * clock_unit).round
      rest_length = ticks_to_mml_with_ties(rest_ticks, ticks_per_beat)
      mml_tokens << "r#{rest_length}"
      current_clock += gap_clocks
    end
    # If gap_clocks <= 0 (overlap or adjacent), no rest needed
    # current_clock stays at previous note's end position

    # Determine actual start position (may differ from MIDI due to overlaps)
    actual_note_start = [current_clock, note_start_clocks].max

    # Calculate note duration using HYBRID approach:
    # - If gap to next note is >= 3 clocks: use actual duration + rest
    # - If gap to next note is < 3 clocks: extend note to next note start (absorb gap)

    note_end_clocks = (note[:end_time] / clock_unit).round
    actual_duration_clocks = note_end_clocks - actual_note_start

    if i + 1 < notes.size
      next_start_clocks = note_start_clocks_array[i + 1]
      gap_to_next = next_start_clocks - note_end_clocks

      if gap_to_next >= 3
        # Gap is expressible in MML (r32 or larger)
        # Use actual duration and let rest fill the gap
        note_duration_clocks = actual_duration_clocks
      else
        # Gap is too small to express (< 3 clocks)
        # Extend note to next note start to absorb the gap
        note_duration_clocks = next_start_clocks - actual_note_start
      end
    else
      # Last note - use actual duration
      note_duration_clocks = actual_duration_clocks
    end

    # Ensure minimum duration (3 clocks = 32nd note)
    note_duration_clocks = [note_duration_clocks, 3].max

    note_duration_ticks = (note_duration_clocks * clock_unit).round

    # Change octave if needed
    if note[:octave] != current_octave
      mml_tokens << "o#{note[:octave]}"
      current_octave = note[:octave]
    end

    # Convert duration to MML with ties
    duration_str = ticks_to_mml_with_ties(note_duration_ticks, ticks_per_beat)
    mml_tokens << "#{note[:note]}#{duration_str}"

    # Update current clock position to where this note ends
    # Use actual_note_start (not note_start_clocks) since overlaps can shift the start
    current_clock = actual_note_start + note_duration_clocks
  end

  # Join tokens with spaces for readability
  mml_sequence = mml_tokens.join(' ')

  {
    comment: "; #{track_name} (Channel #{channel})",
    sequence: mml_sequence,
    bpm: bpm,
    note_count: notes.size
  }
end

# Main conversion function
def convert_midi_to_mml(midi_file, output_file)
  # Load MIDI file
  seq = MIDI::Sequence.new

  File.open(midi_file, 'rb') do |file|
    seq.read(file)
  end

  puts "MIDI File: #{midi_file}"
  puts "Format: #{seq.format}"
  puts "Ticks per beat: #{seq.ppqn}"
  puts "Number of tracks: #{seq.tracks.size}\n"

  # Get tempo at note start time from conductor track (Track 0)
  # Notes typically start at 960 ticks (2 beats), so get tempo at that point
  note_start_time = 960
  performance_tempo = 500_000  # Default 120 BPM
  current_time = 0

  seq.tracks[0].each do |event|
    current_time += event.delta_time

    if event.is_a?(MIDI::Tempo)
      performance_tempo = event.tempo
    end

    if current_time >= note_start_time
      break
    end
  end

  performance_bpm = tempo_to_bpm(performance_tempo)
  puts "Performance tempo: #{performance_bpm} BPM (at #{note_start_time} ticks)\n"

  # Process each track
  mml_tracks = []

  seq.tracks.each_with_index do |track, i|
    # Extract track name from meta events
    track_name = "Track #{i}"
    track.each do |event|
      if event.is_a?(MIDI::MetaEvent) && event.meta_type == MIDI::META_SEQ_NAME
        track_name = event.data_as_str
        break
      end
    end

    # Skip track 0 (tempo/meta) and empty tracks
    if i == 0
      puts "Skipping Track #{i}: #{track_name} (tempo/meta)"
      next
    end

    # Check if track has any note events
    has_notes = track.any? { |e| e.is_a?(MIDI::NoteOn) || e.is_a?(MIDI::NoteOff) }
    unless has_notes
      puts "Skipping Track #{i}: #{track_name} (no notes)"
      next
    end

    puts "Processing Track #{i}: #{track_name}"

    # Determine channel (0-indexed, tracks 1-5 map to channels 0-4)
    channel = i - 1  # Track 1 -> Channel 0, Track 2 -> Channel 1, etc.

    result = convert_track_to_mml(track, seq.ppqn, track_name, channel, performance_tempo)

    if result
      mml_tracks << result
      puts "  Notes: #{result[:note_count]}"
      puts "  BPM: #{result[:bpm]}"
      preview = result[:sequence][0...100]
      puts "  Preview: #{preview}...\n"
    end
  end

  # Output MML file
  File.open(output_file, 'w') do |f|
    f.puts "; ============================================================"
    f.puts "; J.S. Bach: Suite No.3 - Air (BWV 1068)"
    f.puts "; Converted from MIDI to MML format"
    f.puts "; Format: MML as per docs/MML_DESIGN.md"
    f.puts "; INCLUDES RESTS for accurate timing!"
    f.puts "; ============================================================"
    f.puts

    # Write each track
    mml_tracks.each do |track|
      f.puts "; ===== #{track[:comment]} ====="
      f.puts "; BPM: #{track[:bpm]}, Notes: #{track[:note_count]}"
      f.puts track[:sequence]
      f.puts
    end
  end

  puts "\nMML file written to: #{output_file}"
  puts "Total tracks converted: #{mml_tracks.size}"
end

# Command-line interface
if __FILE__ == $PROGRAM_NAME
  if ARGV.size < 1
    puts "Usage: #{$PROGRAM_NAME} <input.mid> [output.mml]"
    puts "Example: #{$PROGRAM_NAME} bach_suite3-2_air.mid bach_air.mml"
    exit 1
  end

  midi_file = ARGV[0]
  output_file = ARGV[1] || midi_file.sub(/\.mid$/i, '.mml')

  unless File.exist?(midi_file)
    puts "Error: MIDI file not found: #{midi_file}"
    exit 1
  end

  convert_midi_to_mml(midi_file, output_file)
end
