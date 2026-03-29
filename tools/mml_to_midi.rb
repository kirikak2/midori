#!/usr/bin/env ruby
# frozen_string_literal: true

# MML to MIDI converter
# Converts MML string to MIDI file for playback on PC

require 'midilib'

class MMLToMIDI
  TICKS_PER_BEAT = 480

  def initialize(bpm: 120)
    @bpm = bpm
    @seq = MIDI::Sequence.new
    @seq.tracks << MIDI::Track.new(@seq)  # Conductor track

    # Set tempo
    @seq.tracks[0].events << MIDI::Tempo.new(MIDI::Tempo.bpm_to_mpq(@bpm))
    @seq.tracks[0].events << MIDI::MetaEvent.new(MIDI::META_SEQ_NAME, 'MML Playback')
  end

  def add_track(mml_string, channel: 0, program: 0, track_name: 'Track')
    track = MIDI::Track.new(@seq)
    @seq.tracks << track

    # Track name
    track.events << MIDI::MetaEvent.new(MIDI::META_SEQ_NAME, track_name)

    # Program change
    track.events << MIDI::ProgramChange.new(channel, program, 0)

    # Parse MML and add note events
    events = parse_mml(mml_string, channel)

    # Sort events by time and add to track
    events.sort_by! { |e| e[:time] }

    current_time = 0
    events.each do |event|
      delta = event[:time] - current_time

      case event[:type]
      when :note_on
        track.events << MIDI::NoteOn.new(event[:channel], event[:note], event[:velocity], delta)
      when :note_off
        track.events << MIDI::NoteOff.new(event[:channel], event[:note], 64, delta)
      end

      current_time = event[:time]
    end

    track
  end

  def save(filename)
    File.open(filename, 'wb') { |f| @seq.write(f) }
    puts "MIDI file saved: #{filename}"
  end

  private

  def parse_mml(mml_str, channel)
    mml = mml_str.gsub(/\s+/, '')  # Remove whitespace

    current_time = 0  # in MIDI ticks
    current_octave = 4
    default_length = 4  # Quarter note
    events = []

    i = 0
    while i < mml.length
      char = mml[i]

      case char
      when 'o'
        # Octave change
        i += 1
        if i < mml.length && mml[i] =~ /\d/
          current_octave = mml[i].to_i
          i += 1
        end

      when 'r'
        # Rest
        i += 1
        length_str, i = parse_length(mml, i)
        duration = length_to_ticks(length_str.empty? ? default_length.to_s : length_str)
        current_time += duration

      when 'a', 'b', 'c', 'd', 'e', 'f', 'g'
        # Note
        note_char = char
        i += 1

        # Check for accidental
        accidental = 0
        if i < mml.length && (mml[i] == '+' || mml[i] == '#')
          accidental = 1
          i += 1
        elsif i < mml.length && mml[i] == '-'
          accidental = -1
          i += 1
        end

        # Parse length
        length_str, i = parse_length(mml, i)
        duration = length_to_ticks(length_str.empty? ? default_length.to_s : length_str)

        # Convert note to MIDI note number
        note_num = note_to_midi(note_char, current_octave, accidental)

        # Add note on and note off events
        events << {
          type: :note_on,
          time: current_time,
          channel: channel,
          note: note_num,
          velocity: 100
        }

        events << {
          type: :note_off,
          time: current_time + duration,
          channel: channel,
          note: note_num
        }

        current_time += duration

      else
        i += 1
      end
    end

    events
  end

  def parse_length(mml, start_pos)
    length_str = String.new
    i = start_pos

    # Parse number with dots and ties
    while i < mml.length
      if mml[i] =~ /[\d.&]/
        length_str << mml[i]
        i += 1
      else
        break
      end
    end

    [length_str, i]
  end

  def length_to_ticks(length_str)
    # Convert MML length notation to MIDI ticks
    # Supports ties: "1&8" = whole note + eighth note

    if length_str.include?('&')
      parts = length_str.split('&')
      return parts.map { |p| length_to_ticks(p) }.sum
    end

    # Parse dots
    dots = length_str.count('.')
    base_str = length_str.gsub('.', '')

    # Base length
    base_length = case base_str
    when '1' then TICKS_PER_BEAT * 4
    when '2' then TICKS_PER_BEAT * 2
    when '4' then TICKS_PER_BEAT
    when '8' then TICKS_PER_BEAT / 2
    when '16' then TICKS_PER_BEAT / 4
    when '32' then TICKS_PER_BEAT / 8
    else TICKS_PER_BEAT  # Default to quarter note
    end

    # Add dots
    total = base_length
    dot_value = base_length / 2
    dots.times do
      total += dot_value
      dot_value /= 2
    end

    total
  end

  def note_to_midi(note_char, octave, accidental)
    # Convert note letter to MIDI note number
    base_notes = {
      'c' => 0,
      'd' => 2,
      'e' => 4,
      'f' => 5,
      'g' => 7,
      'a' => 9,
      'b' => 11
    }

    base = base_notes[note_char.downcase]
    midi_note = (octave + 1) * 12 + base + accidental

    # Clamp to valid MIDI range
    [[midi_note, 0].max, 127].min
  end
end

# Main script
if __FILE__ == $0
  if ARGV.empty?
    puts "Usage: ruby mml_to_midi.rb <mml_file> [output.mid]"
    puts "Example: ruby mml_to_midi.rb bach_air_final.mml bach_air.mid"
    exit 1
  end

  input_file = ARGV[0]
  output_file = ARGV[1] || input_file.gsub(/\.mml$/, '.mid')

  unless File.exist?(input_file)
    puts "Error: Input file not found: #{input_file}"
    exit 1
  end

  # Read MML file
  content = File.read(input_file)

  # Parse MML file format
  channels = []
  lines = content.split("\n")
  current_channel = nil
  bpm = 120

  lines.each do |line|
    if line =~ /Channel (\d+)/
      current_channel = $1.to_i
    elsif line =~ /BPM: (\d+)/
      bpm = $1.to_i
    elsif current_channel && !line.start_with?(';') && line.strip.length > 0
      # MML data line - only add if we have a channel number
      channels << {
        channel: current_channel,
        mml: line.strip
      }
      current_channel = nil  # Reset after reading MML
    end
  end

  if channels.empty?
    puts "Error: No MML data found in file"
    exit 1
  end

  # Create MIDI file
  converter = MMLToMIDI.new(bpm: bpm)

  # Track names and programs (General MIDI)
  track_config = [
    { name: 'Violin I', program: 40 },      # Violin
    { name: 'Violin II', program: 40 },     # Violin
    { name: 'Viola', program: 41 },         # Viola
    { name: 'Continuo 1', program: 43 },    # Contrabass
    { name: 'Continuo 2', program: 43 }     # Contrabass
  ]

  channels.each_with_index do |ch_data, idx|
    config = track_config[idx] || { name: "Track #{idx}", program: 0 }
    puts "Adding track: #{config[:name]} (Channel #{ch_data[:channel]})"
    converter.add_track(
      ch_data[:mml],
      channel: ch_data[:channel],
      program: config[:program],
      track_name: config[:name]
    )
  end

  # Save MIDI file
  converter.save(output_file)

  puts "\nTo play the MIDI file:"
  puts "  timidity #{output_file}"
  puts "  or"
  puts "  fluidsynth -a alsa -m alsa_seq -l -i /usr/share/sounds/sf2/FluidR3_GM.sf2 #{output_file}"
end
