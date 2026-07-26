#!/usr/bin/env ruby
# Debug script to show what events MML parser generates at specific clocks

require_relative '../mrbgems/picoruby-midi-mml/mrblib/midi_mml'

# MML from bach_air.rb
violin1_mml = "r2 o5 f+1&8 b16 g16 f+32 e32 d16 c+16 d16 c+4 o4 b16 a8. o5 a2&16 f+16 c16 o4 b16 o5 e16 d+16 a16 g16 g2&16 e16 o4 b16 a16 o5 d16 c+16 g16 f+16 f+4. g+16 a16 d8 d32 e32 f+8 e16 e16 d16 c+16 o4 b16 b32 o5 c+32 d8. c+16 o4 b16 a2 o5 f+1&8 b16 g16 f+32 e32 d16 c+16 d16 c+4 o4 b16 a8. o5 a2&16 f+16 c16 o4 b16 o5 e16 d+16 a16 g16 g2&16 e16 o4 b16 a16 o5 d16 c+16 g16 f+16 f+4. g+16 a16 d8 d32 e32 f+8 e16 e16 d16 c+16 o4 b16 b32 o5 c+32 d8. c+16 o4 b16 a2 o5 c+4&16 d32 c+32 o4 b32 o5 c+32 o4 a16 o5 a4. c8 o4 b8 o5 b8. a16 g16 f+16 g4&32 f+32 e32 d32 c+16 o4 b16 a+16 b16 o5 c+8. d16 e8. f+16 g4 f+8 e16 d16 c+16 o4 b16 o5 c+16 d32 e32 d16 c+16 o4 b2 o5 d4&16 f+16 e16 d16 b4. a16 g+16 f+32 e32 a16 o4 a8 b8. o5 c+32 d32 c+8. o4 b16 a4 o5 d4. f+16 e16 e4. g16 f+16 f+4. a16 g16 g2 o4 a4&16 o5 c+16 e16 g16 g16 e16 f+4&16 g32 a32 d4&16 f+16 a16 o6 c16 o5 b4. d8 c+16 e16 g4 o4 b8 a8 o5 e16 f+32 g16. f+8 e16 d32 c+32 o4 b8 o5 c+16 d8 c+16 d16 d2 c+4&16 d32 c+32 o4 b32 o5 c+32 o4 a16 o5 a4. c8 o4 b8 o5 b8. a16 g16 f+16 g4&32 f+32 e32 d32 c+16 o4 b16 a+16 b16 o5 c+8. d16 e8. f+16 g4 f+8 e16 d16 c+16 o4 b16 o5 c+16 d32 e32 d16 c+16 o4 b2 o5 d4&16 f+16 e16 d16 b4. a16 g+16 f+32 e32 a16 o4 a8 b8. o5 c+32 d32 c+8. o4 b16 a4 o5 d4. f+16 e16 e4. g16 f+16 f+4. a16 g16 g2 o4 a4&16 o5 c+16 e16 g16 g16 e16 f+4&16 g32 a32 d4&16 f+16 a16 o6 c16 o5 b4. d8 c+16 e16 g4 o4 b8 a8 o5 e16 f+32 g16. f+8 e16 d32 c+32 o4 b8 o5 c+16 d8 c+16 d16 d4.&16.&32"
violin2_mml = "r2 o5 d1&4 o4 b4 a2&8 o5 c16 o4 b16 o5 c8 a16 c16 o4 b16.&32 r4.&32 b8 o5 e16 d16 e16 f+16 g16 e16 o4 a16.&32 r4.&32 a2&8 g+16 a16 b8 g+8 a8 a4 g+8 e2 o5 d1&4 o4 b4 a2&8 o5 c16 o4 b16 o5 c8 a16 c16 o4 b16.&32 r4.&32 b8 o5 e16 d16 e16 f+16 g16 e16 o4 a16.&32 r4.&32 a2&8 g+16 a16 b8 g+8 a8 a4 g+8 e2 a2&16 b16 o5 c8. o4 b16 a16 g16 f+4. o5 d+8 e1&16 d16 c+16 o4 b16 a+16 b16 o5 c+8 o4 b8 b8 b8 a+8 f+2 e4 f+4 o3 b8 o4 e16 f+16 g+16 a16 b4 a4 g+8 a2&8 b16 o5 c16 o4 b16 o5 c+16 d4 c+16 o4 b16 o5 c+16 d+16 e4 d+16 c+16 d+16 e16 f+8. d+16 e16 o4 b16 e4&16 c+16 e16 a16 o5 c+8 o4 a4 o5 c+16 d16 o4 d4. e8 f+4 g2&8 b8 o5 e4&16 d16 c+16 o4 b16 a8 b8 a4 a32 g32 a32 g32 a32 g32 f+32 g32 f+2 a2&16 b16 o5 c8. o4 b16 a16 g16 f+4. o5 d+8 e1&16 d16 c+16 o4 b16 a+16 b16 o5 c+8 o4 b8 b8 b8 a+8 f+2 e4 f+4 o3 b8 o4 e16 f+16 g+16 a16 b4 a4 g+8 a2&8 b16 o5 c16 o4 b16 o5 c+16 d4 c+16 o4 b16 o5 c+16 d+16 e4 d+16 c+16 d+16 e16 f+8. d+16 e16 o4 b16 e4&16 c+16 e16 a16 o5 c+8 o4 a4 o5 c+16 d16 o4 d4. e8 f+4 g2&8 b8 o5 e4&16 d16 c+16 o4 b16 a8 b8 a4 a32 g32 a32 g32 a32 g32 f+32 g32 f+4.&16.&32"
viola_mml = "r2 o4 a2 b2 o3 b4 o4 e4 e2&8 d+8 d+8 e8 f+16.&32 r4.&32 e8 o3 b4 o4 e8 e16.&32 r4.&32 d4. e8 f+8 d8 o3 b8 o4 e4 f+8 o3 a8 o4 e8 c+2 a2 b2 o3 b4 o4 e4 e2&8 d+8 d+8 e8 f+16.&32 r4.&32 e8 o3 b4 o4 e8 e16.&32 r4.&32 d4. e8 f+8 d8 o3 b8 o4 e4 f+8 o3 a8 o4 e8 c+2 e2&8 d+16 e16 f+4&16 g16 a16 f+16 d+8 b8 b4 o3 b4 o4 c+16 d16 e16 f+16 g16 f+16 g16 e16 f+8 e16 d16 c+8 f+8 f+8 e16 d16 g8 f+16 e16 d2 o3 b8 o4 b8 a16 g+16 a8 g+8. f+16 e4. e8 f+8 e8 e8. d16 c+16 d16 e16 c+16 o3 a8 o4 d4 o3 b4 o4 e4 c+4 f+4 d+8 o3 b4&16 o4 b16 g16 e16 a8 g8 f+8 e8 d4 a4. g8 a4 d2 e16 o3 b16 o4 e16 g16 b16 a16 g16 f+16 e8 a4 g8 f+4 e8 o3 a8 a2 o4 e2&8 d+16 e16 f+4&16 g16 a16 f+16 d+8 b8 b4 o3 b4 o4 c+16 d16 e16 f+16 g16 f+16 g16 e16 f+8 e16 d16 c+8 f+8 f+8 e16 d16 g8 f+16 e16 d2 o3 b8 o4 b8 a16 g+16 a8 g+8. f+16 e4. e8 f+8 e8 e8. d16 c+16 d16 e16 c+16 o3 a8 o4 d4 o3 b4 o4 e4 c+4 f+4 d+8 o3 b4&16 o4 b16 g16 e16 a8 g8 f+8 e8 d4 a4. g8 a4 d2 e16 o3 b16 o4 e16 g16 b16 a16 g16 f+16 e8 a4 g8 f+4 e8 o3 a8 a4.&16.&32"
continuo1_mml = "r2 o3 d8 o4 d8 c+8 o3 c+8 o2 b8 o3 b8 a8 o2 a8 g8 o3 g8 g+8 o2 g+8 a8 o3 a8 g8 o2 g8 f+8 o3 f+8 e8 o2 e8 d+8 o3 d+8 o2 b8 o3 b8 o2 e8 o3 e8 d8 o2 d8 c+8 o3 c+8 o2 g8 o3 a8 d8 o4 d8 c+8 o3 c+8 o2 b8 o3 b8 g+8 e8 a8 d8 e8 o2 e8 a16 b16 o3 c+16 d16 e16 g16 f+16 e16 d8 o4 d8 c+8 o3 c+8 o2 b8 o3 b8 a8 o2 a8 g8 o3 g8 g+8 o2 g+8 a8 o3 a8 g8 o2 g8 f+8 o3 f+8 e8 o2 e8 d+8 o3 d+8 o2 b8 o3 b8 o2 e8 o3 e8 d8 o2 d8 c+8 o3 c+8 o2 g8 o3 a8 d8 o4 d8 c+8 o3 c+8 o2 b8 o3 b8 g+8 e8 a8 d8 e8 o2 e8 a16 b16 o3 c+16 d16 e16 g16 f+16 e16 o2 a8 o3 a8 g8 o2 g8 f+8 o3 f+8 e8 o2 e8 d+8 o3 d+8 f+8 o2 b8 o3 e8 o4 e8 d8 o3 d8 c+8 o4 c+8 o3 b8 o2 b8 a+8 b8 o3 c+8 o2 a+8 b8 o3 g8 e8 f+8 o2 b8 o3 b8 a8 o2 a8 g+8 o3 g+8 f+8 o2 f+8 e8 o3 e8 d8 o2 d8 c+8 o3 c+8 d8 e8 o2 a8 o3 a8 g8 o2 g8 f+8 o3 f+8 g8 o2 g8 g+8 o3 g+8 a8 o2 a8 a+8 o3 a+8 b8 o2 b8 o3 e8 o4 e8 d8 o3 d8 c+8 o4 c+8 o3 a8 o4 c+8 d8 o3 d8 c8 o4 c8 o3 b8 o2 b8 a8 o3 a8 g8 o2 g8 f+8 o3 f+8 e8 o2 e8 d8 o3 d8 c+8 o2 a8 o3 d8 g8 a8 g8 a8 o2 a8 d2 a8 o3 a8 g8 o2 g8 f+8 o3 f+8 e8 o2 e8 d+8 o3 d+8 f+8 o2 b8 o3 e8 o4 e8 d8 o3 d8 c+8 o4 c+8 o3 b8 o2 b8 a+8 b8 o3 c+8 o2 a+8 b8 o3 g8 e8 f+8 o2 b8 o3 b8 a8 o2 a8 g+8 o3 g+8 f+8 o2 f+8 e8 o3 e8 d8 o2 d8 c+8 o3 c+8 d8 e8 o2 a8 o3 a8 g8 o2 g8 f+8 o3 f+8 g8 o2 g8 g+8 o3 g+8 a8 o2 a8 a+8 o3 a+8 b8 o2 b8 o3 e8 o4 e8 d8 o3 d8 c+8 o4 c+8 o3 a8 o4 c+8 d8 o3 d8 c8 o4 c8 o3 b8 o2 b8 a8 o3 a8 g8 o2 g8 f+8 o3 f+8 e8 o2 e8 d8 o3 d8 c+8 o2 a8 o3 d8 g8 a8 g8 a8 o2 a8 d4.&16.&32"
continuo2_mml = "r2 o2 d8 o3 d8 c+8 o2 c+8 o1 b8 o2 b8 a8 o1 a8 g8 o2 g8 g+8 o1 g+8 a8 o2 a8 g8 o1 g8 f+8 o2 f+8 e8 o1 e8 d+8 o2 d+8 o1 b8 o2 b8 o1 e8 o2 e8 d8 o1 d8 c+8 o2 c+8 o1 g8 o2 a8 d8 o3 d8 c+8 o2 c+8 o1 b8 o2 b8 g+8 e8 a8 d8 e8 o1 e8 a16 b16 o2 c+16 d16 e16 g16 f+16 e16 d8 o3 d8 c+8 o2 c+8 o1 b8 o2 b8 a8 o1 a8 g8 o2 g8 g+8 o1 g+8 a8 o2 a8 g8 o1 g8 f+8 o2 f+8 e8 o1 e8 d+8 o2 d+8 o1 b8 o2 b8 o1 e8 o2 e8 d8 o1 d8 c+8 o2 c+8 o1 g8 o2 a8 d8 o3 d8 c+8 o2 c+8 o1 b8 o2 b8 g+8 e8 a8 d8 e8 o1 e8 a16 b16 o2 c+16 d16 e16 g16 f+16 e16 o1 a8 o2 a8 g8 o1 g8 f+8 o2 f+8 e8 o1 e8 d+8 o2 d+8 f+8 o1 b8 o2 e8 o3 e8 d8 o2 d8 c+8 o3 c+8 o2 b8 o1 b8 a+8 b8 o2 c+8 o1 a+8 b8 o2 g8 e8 f+8 o1 b8 o2 b8 a8 o1 a8 g+8 o2 g+8 f+8 o1 f+8 e8 o2 e8 d8 o1 d8 c+8 o2 c+8 d8 e8 o1 a8 o2 a8 g8 o1 g8 f+8 o2 f+8 g8 o1 g8 g+8 o2 g+8 a8 o1 a8 a+8 o2 a+8 b8 o1 b8 o2 e8 o3 e8 d8 o2 d8 c+8 o3 c+8 o2 a8 o3 c+8 d8 o2 d8 c8 o3 c8 o2 b8 o1 b8 a8 o2 a8 g8 o1 g8 f+8 o2 f+8 e8 o1 e8 d8 o2 d8 c+8 o1 a8 o2 d8 g8 a8 g8 a8 o1 a8 d2 a8 o2 a8 g8 o1 g8 f+8 o2 f+8 e8 o1 e8 d+8 o2 d+8 f+8 o1 b8 o2 e8 o3 e8 d8 o2 d8 c+8 o3 c+8 o2 b8 o1 b8 a+8 b8 o2 c+8 o1 a+8 b8 o2 g8 e8 f+8 o1 b8 o2 b8 a8 o1 a8 g+8 o2 g+8 f+8 o1 f+8 e8 o2 e8 d8 o1 d8 c+8 o2 c+8 d8 e8 o1 a8 o2 a8 g8 o1 g8 f+8 o2 f+8 g8 o1 g8 g+8 o2 g+8 a8 o1 a8 a+8 o2 a+8 b8 o1 b8 o2 e8 o3 e8 d8 o2 d8 c+8 o3 c+8 o2 a8 o3 c+8 d8 o2 d8 c8 o3 c8 o2 b8 o1 b8 a8 o2 a8 g8 o1 g8 f+8 o2 f+8 e8 o1 e8 d8 o2 d8 c+8 o1 a8 o2 d8 g8 a8 g8 a8 o1 a8 d4.&16.&32"

puts "Parsing MML sequences..."
puts

# Parse sequences
seqs = [
  {name: "Violin I",    mml: violin1_mml,   channel: 0},
  {name: "Violin II",   mml: violin2_mml,   channel: 1},
  {name: "Viola",       mml: viola_mml,     channel: 2},
  {name: "Continuo 1",  mml: continuo1_mml, channel: 3},
  {name: "Continuo 2",  mml: continuo2_mml, channel: 4}
].map do |s|
  seq = MIDI::MML::Sequence.new(s[:mml], channel: s[:channel])
  {name: s[:name], seq: seq, channel: s[:channel]}
end

# Check events at specific clocks
check_clocks = [48, 132, 144, 156, 162]

check_clocks.each do |clock|
  puts "=" * 60
  puts "Events at CLOCK #{clock}:"
  puts "=" * 60

  seqs.each do |s|
    events = s[:seq].events_at(clock)
    next if events.empty?

    puts "\n#{s[:name]} (ch=#{s[:channel]}):"
    events.each do |e|
      case e[:type]
      when :note_on
        puts "  note_on  note=#{e[:note]} vel=#{e[:velocity]} duration=#{e[:duration_clocks]} clocks"
      when :note_off
        puts "  note_off note=#{e[:note]}"
      end
    end
  end
  puts
end

# Show total length
puts "=" * 60
puts "Sequence lengths:"
puts "=" * 60
seqs.each do |s|
  puts "#{s[:name]}: #{s[:seq].total_length} clocks"
end

# ============================================================
# Dump every event up to DUMP_UNTIL_CLOCK in the order the
# on-device CombinedPlayer would emit them:
#   1. note_off events at this clock (flushed first)
#   2. note_on events at this clock (sent as one trigger_batch)
# Within each group we keep the original sequence-add order
# (Violin I, II, Viola, Continuo 1, Continuo 2) so a side-by-side
# diff against a serial-log capture lines up cleanly.
# Line format: `[c=<clock> sc=<clock>] on|off ch=<n> n=<note> ...`
# ============================================================
DUMP_UNTIL_CLOCK = 480

puts
puts "=" * 60
puts "Theoretical event timeline (clock 0..#{DUMP_UNTIL_CLOCK})"
puts "=" * 60

# Collect all events with their sequence index for stable secondary sort
all_events = []
seqs.each_with_index do |s, idx|
  s[:seq].events.each do |e|
    next if e[:clock] > DUMP_UNTIL_CLOCK
    all_events << {
      clock: e[:clock],
      seq_idx: idx,
      channel: s[:channel],
      event: e,
    }
  end
end

# Sort: by clock, then note_off before note_on, then by sequence index
all_events.sort_by! do |item|
  type_rank = item[:event][:type] == :note_off ? 0 : 1
  [item[:clock], type_rank, item[:seq_idx]]
end

all_events.each do |item|
  e = item[:event]
  c = item[:clock]
  ch = item[:channel]
  case e[:type]
  when :note_on
    # CombinedPlayer multiplies duration_clocks by 100 for duration_ms
    duration_ms = (e[:duration_clocks] || 24) * 100
    puts "[c=#{c} sc=#{c}] on  ch=#{ch} n=#{e[:note]} v=#{e[:velocity]} d=#{duration_ms}"
  when :note_off
    puts "[c=#{c} sc=#{c}] off ch=#{ch} n=#{e[:note]}"
  end
end
