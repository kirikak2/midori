#!/usr/bin/env ruby
# Debug script to trace how tied notes are parsed

$:.unshift File.expand_path('../mrbgems/picoruby-midi/mrblib', __dir__)

# Monkey-patch the Sequence class to add debug output
module MIDI
  module MML
    class Sequence
      # Override parse_note with debug logging
      def parse_note(note_char)
        puts "\n=== parse_note('#{note_char}') at position #{@pos}, @clock=#{@clock} ==="

        semitone = NOTE_MAP[note_char]

        # Check for sharp/flat
        if peek == '+' || peek == '#'
          @pos += 1
          semitone += 1
          puts "  Found sharp: semitone=#{semitone}"
        elsif peek == '-'
          @pos += 1
          semitone -= 1
          puts "  Found flat: semitone=#{semitone}"
        end

        # Calculate MIDI note number
        midi_note = (@octave + 1) * 12 + semitone
        puts "  MIDI note: #{midi_note} (octave=#{@octave})"

        # Get note length
        length, dots = parse_length
        clocks = length_to_clocks(length, dots)
        puts "  Length: #{length}, dots: #{dots}, clocks: #{clocks}"

        # Check for tie
        is_tied = false
        save_pos = @pos
        while @pos < @mml.length && @mml[@pos] == ' '
          @pos += 1
        end
        if @pos < @mml.length && @mml[@pos] == '&'
          @pos += 1
          is_tied = true
          puts "  Found tie '&' at position #{@pos-1}"
        else
          @pos = save_pos
          puts "  No tie found, next char: '#{peek}'"
        end

        if @tie_note && @tie_note[:note] == midi_note
          # Continue tied note - extend duration
          puts "  Continuing tied note: #{@tie_note.inspect}"
          @tie_note[:duration] += clocks
          puts "  New duration: #{@tie_note[:duration]}"
          if !is_tied
            # End of tie - emit note off
            note_off_clock = @tie_note[:start] + @tie_note[:duration]
            puts "  End of tie: note_off at clock #{note_off_clock}"
            @events << {
              type: :note_off,
              clock: note_off_clock,
              note: midi_note,
              channel: @channel
            }
            @tie_note = nil
          else
            puts "  Tie continues..."
          end
        else
          # New note
          if @tie_note
            # Previous tie ended without matching note
            puts "  Previous tie ended: #{@tie_note.inspect}"
            @events << {
              type: :note_off,
              clock: @tie_note[:start] + @tie_note[:duration],
              note: @tie_note[:note],
              channel: @channel
            }
            @tie_note = nil
          end

          # Note on
          puts "  Creating note_on at clock #{@clock}"
          @events << {
            type: :note_on,
            clock: @clock,
            note: midi_note,
            velocity: @velocity,
            channel: @channel,
            duration_clocks: clocks
          }

          if is_tied
            @tie_note = { note: midi_note, start: @clock, duration: clocks }
            puts "  Starting tie: #{@tie_note.inspect}"
          else
            # Note off
            note_off_clock = @clock + clocks
            puts "  Creating note_off at clock #{note_off_clock}"
            @events << {
              type: :note_off,
              clock: note_off_clock,
              note: midi_note,
              channel: @channel
            }
          end
        end

        puts "  @clock += #{clocks}"
        @clock += clocks
        puts "  @clock is now #{@clock}"
      end
    end
  end
end

require 'midi_mml'

# Test just the first few notes of Violin I
test_mml = "r2 o5 f+1&8 b16"

puts "Testing MML: #{test_mml}"
puts "=" * 70

seq = MIDI::MML::Sequence.new(test_mml, channel: 0)

puts "\n" + "=" * 70
puts "Final events:"
puts "=" * 70
seq.events.each do |e|
  puts "clock=#{e[:clock]} #{e[:type]} note=#{e[:note]}"
end
