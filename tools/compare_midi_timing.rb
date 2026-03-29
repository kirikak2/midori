#!/usr/bin/env ruby
# frozen_string_literal: true

# Compare timing between original MIDI and MML-generated MIDI

require 'midilib'

def extract_note_events(midi_file, track_index)
  seq = MIDI::Sequence.new
  File.open(midi_file, 'rb') { |f| seq.read(f) }

  return nil if track_index >= seq.tracks.size

  track = seq.tracks[track_index]
  ticks_per_beat = seq.ppqn

  current_time = 0
  active_notes = {}
  note_events = []

  track.events.each do |event|
    current_time += event.delta_time

    case event
    when MIDI::NoteOn
      if event.velocity > 0
        active_notes[event.note] = current_time
      else
        # Velocity 0 = note off
        if active_notes.key?(event.note)
          start_time = active_notes.delete(event.note)
          note_events << {
            note: event.note,
            start: start_time,
            end: current_time,
            duration: current_time - start_time
          }
        end
      end
    when MIDI::NoteOff
      if active_notes.key?(event.note)
        start_time = active_notes.delete(event.note)
        note_events << {
          note: event.note,
          start: start_time,
          end: current_time,
          duration: current_time - start_time
        }
      end
    end
  end

  { events: note_events.sort_by { |e| e[:start] }, ppqn: ticks_per_beat }
end

def note_name(note_num)
  names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
  octave = (note_num / 12) - 1
  "#{names[note_num % 12]}#{octave}"
end

# Main
if ARGV.size < 2
  puts "Usage: ruby compare_midi_timing.rb <original.mid> <generated.mid> [track_num]"
  puts "Example: ruby compare_midi_timing.rb bach_suite3-2_air.mid bach_air_test.mid 1"
  exit 1
end

original_file = ARGV[0]
generated_file = ARGV[1]
track_num = (ARGV[2] || '1').to_i

unless File.exist?(original_file)
  puts "Error: Original file not found: #{original_file}"
  exit 1
end

unless File.exist?(generated_file)
  puts "Error: Generated file not found: #{generated_file}"
  exit 1
end

puts "="*100
puts "MIDI Timing Comparison"
puts "="*100
puts "Original:  #{original_file} (Track #{track_num})"
puts "Generated: #{generated_file} (Track #{track_num})"
puts "="*100
puts

original_data = extract_note_events(original_file, track_num)
generated_data = extract_note_events(generated_file, track_num)

if original_data.nil?
  puts "Error: Track #{track_num} not found in original file"
  exit 1
end

if generated_data.nil?
  puts "Error: Track #{track_num} not found in generated file"
  exit 1
end

original_events = original_data[:events]
generated_events = generated_data[:events]

puts "Original PPQ:  #{original_data[:ppqn]}"
puts "Generated PPQ: #{generated_data[:ppqn]}"
puts
puts "Original notes:  #{original_events.size}"
puts "Generated notes: #{generated_events.size}"
puts
puts "="*100

# Compare first N notes
compare_count = [original_events.size, generated_events.size, 20].min

puts "\nFirst #{compare_count} notes comparison:"
puts "="*100
puts sprintf("%-4s %-6s %-12s %-12s %-8s %-12s %-12s %-8s",
             "#", "Note", "Orig Start", "Gen Start", "Diff", "Orig Dur", "Gen Dur", "Dur Diff")
puts "-"*100

max_diff = 0
total_diff = 0
matches = 0

compare_count.times do |i|
  orig = original_events[i]
  gen = generated_events[i]

  # Convert to same PPQ for comparison
  orig_start_normalized = (orig[:start].to_f / original_data[:ppqn] * 480).round
  gen_start_normalized = (gen[:start].to_f / generated_data[:ppqn] * 480).round

  orig_dur_normalized = (orig[:duration].to_f / original_data[:ppqn] * 480).round
  gen_dur_normalized = (gen[:duration].to_f / generated_data[:ppqn] * 480).round

  diff = gen_start_normalized - orig_start_normalized
  dur_diff = gen_dur_normalized - orig_dur_normalized

  max_diff = [max_diff, diff.abs].max
  total_diff += diff.abs
  matches += 1 if diff.abs <= 1

  status = diff.abs <= 1 ? "✓" : "✗"

  puts sprintf("%-4d %-6s %-12d %-12d %-8s %-12d %-12d %-8s %s",
               i+1,
               note_name(orig[:note]),
               orig_start_normalized,
               gen_start_normalized,
               diff == 0 ? "0" : sprintf("%+d", diff),
               orig_dur_normalized,
               gen_dur_normalized,
               dur_diff == 0 ? "0" : sprintf("%+d", dur_diff),
               status)
end

puts "-"*100
puts "\nStatistics:"
puts "  Perfect matches (≤1 tick): #{matches}/#{compare_count}"
puts "  Maximum timing difference: #{max_diff} ticks"
puts "  Average timing difference: #{total_diff.to_f / compare_count} ticks"
puts
puts "Result: #{matches == compare_count ? '✓ EXCELLENT' : matches >= compare_count * 0.9 ? '✓ GOOD' : '✗ NEEDS IMPROVEMENT'}"
