# ============================================================
# J.S. Bach: Suite No.3 - Air (BWV 1068)
# MML Playback Script for Midori
# ============================================================

require "midi"
require "sam2695"
require "ui"

UI.log("Loading Bach Suite No.3 - Air...")

# Initialize MIDI device
sam = SAM2695.new(17, 18)
device = MIDI::Device.new(sam)

# ===== MML Sequences =====

# Channel 0: Violin I (284 notes) - v6 strict timing
violin1_mml = <<~MML
  r2 o5 f+1&8 b16 g16 f+32 e32 d16 c+16 d16 c+4 o4 b16 a8. o5 a2&16 f+16 c16 o4 b16 o5 e16 d+16 a16 g16 g2&16 e16 o4 b16 a16 o5 d16 c+16 g16 f+16 f+4. g+16 a16 d8 d32 e32 f+8 e16 e16 d16 c+16 o4 b16 b32 o5 c+32 d8. c+16 o4 b16 a2 o5 f+1&8 b16 g16 f+32 e32 d16 c+16 d16 c+4 o4 b16 a8. o5 a2&16 f+16 c16 o4 b16 o5 e16 d+16 a16 g16 g2&16 e16 o4 b16 a16 o5 d16 c+16 g16 f+16 f+4. g+16 a16 d8 d32 e32 f+8 e16 e16 d16 c+16 o4 b16 b32 o5 c+32 d8. c+16 o4 b16 a2 o5 c+4&16 d32 c+32 o4 b32 o5 c+32 o4 a16 o5 a4. c8 o4 b8 o5 b8. a16 g16 f+16 g4&32 f+32 e32 d32 c+16 o4 b16 a+16 b16 o5 c+8. d16 e8. f+16 g4 f+8 e16 d16 c+16 o4 b16 o5 c+16 d32 e32 d16 c+16 o4 b2 o5 d4&16 f+16 e16 d16 b4. a16 g+16 f+32 e32 a16 o4 a8 b8. o5 c+32 d32 c+8. o4 b16 a4 o5 d4. f+16 e16 e4. g16 f+16 f+4. a16 g16 g2 o4 a4&16 o5 c+16 e16 g16 g16 e16 f+4&16 g32 a32 d4&16 f+16 a16 o6 c16 o5 b4. d8 c+16 e16 g4 o4 b8 a8 o5 e16 f+32 g16. f+8 e16 d32 c+32 o4 b8 o5 c+16 d8 c+16 d16 d2 c+4&16 d32 c+32 o4 b32 o5 c+32 o4 a16 o5 a4. c8 o4 b8 o5 b8. a16 g16 f+16 g4&32 f+32 e32 d32 c+16 o4 b16 a+16 b16 o5 c+8. d16 e8. f+16 g4 f+8 e16 d16 c+16 o4 b16 o5 c+16 d32 e32 d16 c+16 o4 b2 o5 d4&16 f+16 e16 d16 b4. a16 g+16 f+32 e32 a16 o4 a8 b8. o5 c+32 d32 c+8. o4 b16 a4 o5 d4. f+16 e16 e4. g16 f+16 f+4. a16 g16 g2 o4 a4&16 o5 c+16 e16 g16 g16 e16 f+4&16 g32 a32 d4&16 f+16 a16 o6 c16 o5 b4. d8 c+16 e16 g4 o4 b8 a8 o5 e16 f+32 g16. f+8 e16 d32 c+32 o4 b8 o5 c+16 d8 c+16 d16 d4.&16.&32
MML

# Channel 1: Violin II (208 notes) - v6 strict timing
violin2_mml = <<~MML
  r2 o5 d1&4 o4 b4 a2&8 o5 c16 o4 b16 o5 c8 a16 c16 o4 b16.&32 r4.&32 b8 o5 e16 d16 e16 f+16 g16 e16 o4 a16.&32 r4.&32 a2&8 g+16 a16 b8 g+8 a8 a4 g+8 e2 o5 d1&4 o4 b4 a2&8 o5 c16 o4 b16 o5 c8 a16 c16 o4 b16.&32 r4.&32 b8 o5 e16 d16 e16 f+16 g16 e16 o4 a16.&32 r4.&32 a2&8 g+16 a16 b8 g+8 a8 a4 g+8 e2 a2&16 b16 o5 c8. o4 b16 a16 g16 f+4. o5 d+8 e1&16 d16 c+16 o4 b16 a+16 b16 o5 c+8 o4 b8 b8 b8 a+8 f+2 e4 f+4 o3 b8 o4 e16 f+16 g+16 a16 b4 a4 g+8 a2&8 b16 o5 c16 o4 b16 o5 c+16 d4 c+16 o4 b16 o5 c+16 d+16 e4 d+16 c+16 d+16 e16 f+8. d+16 e16 o4 b16 e4&16 c+16 e16 a16 o5 c+8 o4 a4 o5 c+16 d16 o4 d4. e8 f+4 g2&8 b8 o5 e4&16 d16 c+16 o4 b16 a8 b8 a4 a32 g32 a32 g32 a32 g32 f+32 g32 f+2 a2&16 b16 o5 c8. o4 b16 a16 g16 f+4. o5 d+8 e1&16 d16 c+16 o4 b16 a+16 b16 o5 c+8 o4 b8 b8 b8 a+8 f+2 e4 f+4 o3 b8 o4 e16 f+16 g+16 a16 b4 a4 g+8 a2&8 b16 o5 c16 o4 b16 o5 c+16 d4 c+16 o4 b16 o5 c+16 d+16 e4 d+16 c+16 d+16 e16 f+8. d+16 e16 o4 b16 e4&16 c+16 e16 a16 o5 c+8 o4 a4 o5 c+16 d16 o4 d4. e8 f+4 g2&8 b8 o5 e4&16 d16 c+16 o4 b16 a8 b8 a4 a32 g32 a32 g32 a32 g32 f+32 g32 f+4.&16.&32
MML

# Channel 2: Viola (212 notes) - v6 strict timing
viola_mml = <<~MML
  r2 o4 a2 b2 o3 b4 o4 e4 e2&8 d+8 d+8 e8 f+16.&32 r4.&32 e8 o3 b4 o4 e8 e16.&32 r4.&32 d4. e8 f+8 d8 o3 b8 o4 e4 f+8 o3 a8 o4 e8 c+2 a2 b2 o3 b4 o4 e4 e2&8 d+8 d+8 e8 f+16.&32 r4.&32 e8 o3 b4 o4 e8 e16.&32 r4.&32 d4. e8 f+8 d8 o3 b8 o4 e4 f+8 o3 a8 o4 e8 c+2 e2&8 d+16 e16 f+4&16 g16 a16 f+16 d+8 b8 b4 o3 b4 o4 c+16 d16 e16 f+16 g16 f+16 g16 e16 f+8 e16 d16 c+8 f+8 f+8 e16 d16 g8 f+16 e16 d2 o3 b8 o4 b8 a16 g+16 a8 g+8. f+16 e4. e8 f+8 e8 e8. d16 c+16 d16 e16 c+16 o3 a8 o4 d4 o3 b4 o4 e4 c+4 f+4 d+8 o3 b4&16 o4 b16 g16 e16 a8 g8 f+8 e8 d4 a4. g8 a4 d2 e16 o3 b16 o4 e16 g16 b16 a16 g16 f+16 e8 a4 g8 f+4 e8 o3 a8 a2 o4 e2&8 d+16 e16 f+4&16 g16 a16 f+16 d+8 b8 b4 o3 b4 o4 c+16 d16 e16 f+16 g16 f+16 g16 e16 f+8 e16 d16 c+8 f+8 f+8 e16 d16 g8 f+16 e16 d2 o3 b8 o4 b8 a16 g+16 a8 g+8. f+16 e4. e8 f+8 e8 e8. d16 c+16 d16 e16 c+16 o3 a8 o4 d4 o3 b4 o4 e4 c+4 f+4 d+8 o3 b4&16 o4 b16 g16 e16 a8 g8 f+8 e8 d4 a4. g8 a4 d2 e16 o3 b16 o4 e16 g16 b16 a16 g16 f+16 e8 a4 g8 f+4 e8 o3 a8 a4.&16.&32
MML

# Channel 3: Continuo (Bass line 1) (290 notes) - v6 strict timing
continuo1_mml = <<~MML
  r2 o3 d8 o4 d8 c+8 o3 c+8 o2 b8 o3 b8 a8 o2 a8 g8 o3 g8 g+8 o2 g+8 a8 o3 a8 g8 o2 g8 f+8 o3 f+8 e8 o2 e8 d+8 o3 d+8 o2 b8 o3 b8 o2 e8 o3 e8 d8 o2 d8 c+8 o3 c+8 o2 g8 o3 a8 d8 o4 d8 c+8 o3 c+8 o2 b8 o3 b8 g+8 e8 a8 d8 e8 o2 e8 a16 b16 o3 c+16 d16 e16 g16 f+16 e16 d8 o4 d8 c+8 o3 c+8 o2 b8 o3 b8 a8 o2 a8 g8 o3 g8 g+8 o2 g+8 a8 o3 a8 g8 o2 g8 f+8 o3 f+8 e8 o2 e8 d+8 o3 d+8 o2 b8 o3 b8 o2 e8 o3 e8 d8 o2 d8 c+8 o3 c+8 o2 g8 o3 a8 d8 o4 d8 c+8 o3 c+8 o2 b8 o3 b8 g+8 e8 a8 d8 e8 o2 e8 a16 b16 o3 c+16 d16 e16 g16 f+16 e16 o2 a8 o3 a8 g8 o2 g8 f+8 o3 f+8 e8 o2 e8 d+8 o3 d+8 f+8 o2 b8 o3 e8 o4 e8 d8 o3 d8 c+8 o4 c+8 o3 b8 o2 b8 a+8 b8 o3 c+8 o2 a+8 b8 o3 g8 e8 f+8 o2 b8 o3 b8 a8 o2 a8 g+8 o3 g+8 f+8 o2 f+8 e8 o3 e8 d8 o2 d8 c+8 o3 c+8 d8 e8 o2 a8 o3 a8 g8 o2 g8 f+8 o3 f+8 g8 o2 g8 g+8 o3 g+8 a8 o2 a8 a+8 o3 a+8 b8 o2 b8 o3 e8 o4 e8 d8 o3 d8 c+8 o4 c+8 o3 a8 o4 c+8 d8 o3 d8 c8 o4 c8 o3 b8 o2 b8 a8 o3 a8 g8 o2 g8 f+8 o3 f+8 e8 o2 e8 d8 o3 d8 c+8 o2 a8 o3 d8 g8 a8 g8 a8 o2 a8 d2 a8 o3 a8 g8 o2 g8 f+8 o3 f+8 e8 o2 e8 d+8 o3 d+8 f+8 o2 b8 o3 e8 o4 e8 d8 o3 d8 c+8 o4 c+8 o3 b8 o2 b8 a+8 b8 o3 c+8 o2 a+8 b8 o3 g8 e8 f+8 o2 b8 o3 b8 a8 o2 a8 g+8 o3 g+8 f+8 o2 f+8 e8 o3 e8 d8 o2 d8 c+8 o3 c+8 d8 e8 o2 a8 o3 a8 g8 o2 g8 f+8 o3 f+8 g8 o2 g8 g+8 o3 g+8 a8 o2 a8 a+8 o3 a+8 b8 o2 b8 o3 e8 o4 e8 d8 o3 d8 c+8 o4 c+8 o3 a8 o4 c+8 d8 o3 d8 c8 o4 c8 o3 b8 o2 b8 a8 o3 a8 g8 o2 g8 f+8 o3 f+8 e8 o2 e8 d8 o3 d8 c+8 o2 a8 o3 d8 g8 a8 g8 a8 o2 a8 d4.&16.&32
MML

# Channel 4: Continuo (Bass line 2) (290 notes) - v6 strict timing, one octave lower
continuo2_mml = <<~MML
  r2 o2 d8 o3 d8 c+8 o2 c+8 o1 b8 o2 b8 a8 o1 a8 g8 o2 g8 g+8 o1 g+8 a8 o2 a8 g8 o1 g8 f+8 o2 f+8 e8 o1 e8 d+8 o2 d+8 o1 b8 o2 b8 o1 e8 o2 e8 d8 o1 d8 c+8 o2 c+8 o1 g8 o2 a8 d8 o3 d8 c+8 o2 c+8 o1 b8 o2 b8 g+8 e8 a8 d8 e8 o1 e8 a16 b16 o2 c+16 d16 e16 g16 f+16 e16 d8 o3 d8 c+8 o2 c+8 o1 b8 o2 b8 a8 o1 a8 g8 o2 g8 g+8 o1 g+8 a8 o2 a8 g8 o1 g8 f+8 o2 f+8 e8 o1 e8 d+8 o2 d+8 o1 b8 o2 b8 o1 e8 o2 e8 d8 o1 d8 c+8 o2 c+8 o1 g8 o2 a8 d8 o3 d8 c+8 o2 c+8 o1 b8 o2 b8 g+8 e8 a8 d8 e8 o1 e8 a16 b16 o2 c+16 d16 e16 g16 f+16 e16 o1 a8 o2 a8 g8 o1 g8 f+8 o2 f+8 e8 o1 e8 d+8 o2 d+8 f+8 o1 b8 o2 e8 o3 e8 d8 o2 d8 c+8 o3 c+8 o2 b8 o1 b8 a+8 b8 o2 c+8 o1 a+8 b8 o2 g8 e8 f+8 o1 b8 o2 b8 a8 o1 a8 g+8 o2 g+8 f+8 o1 f+8 e8 o2 e8 d8 o1 d8 c+8 o2 c+8 d8 e8 o1 a8 o2 a8 g8 o1 g8 f+8 o2 f+8 g8 o1 g8 g+8 o2 g+8 a8 o1 a8 a+8 o2 a+8 b8 o1 b8 o2 e8 o3 e8 d8 o2 d8 c+8 o3 c+8 o2 a8 o3 c+8 d8 o2 d8 c8 o3 c8 o2 b8 o1 b8 a8 o2 a8 g8 o1 g8 f+8 o2 f+8 e8 o1 e8 d8 o2 d8 c+8 o1 a8 o2 d8 g8 a8 g8 a8 o1 a8 d2 a8 o2 a8 g8 o1 g8 f+8 o2 f+8 e8 o1 e8 d+8 o2 d+8 f+8 o1 b8 o2 e8 o3 e8 d8 o2 d8 c+8 o3 c+8 o2 b8 o1 b8 a+8 b8 o2 c+8 o1 a+8 b8 o2 g8 e8 f+8 o1 b8 o2 b8 a8 o1 a8 g+8 o2 g+8 f+8 o1 f+8 e8 o2 e8 d8 o1 d8 c+8 o2 c+8 d8 e8 o1 a8 o2 a8 g8 o1 g8 f+8 o2 f+8 g8 o1 g8 g+8 o2 g+8 a8 o1 a8 a+8 o2 a+8 b8 o1 b8 o2 e8 o3 e8 d8 o2 d8 c+8 o3 c+8 o2 a8 o3 c+8 d8 o2 d8 c8 o3 c8 o2 b8 o1 b8 a8 o2 a8 g8 o1 g8 f+8 o2 f+8 e8 o1 e8 d8 o2 d8 c+8 o1 a8 o2 d8 g8 a8 g8 a8 o1 a8 d4.&16.&32
MML

device.program_change(41, channel: 0) # Violin I
device.program_change(41, channel: 1) # Violin II
device.program_change(42, channel: 2) # Viola
device.program_change(44, channel: 3) # Continuo 1 (Acoustic Bass)
device.program_change(44, channel: 4) # Continuo 2 (Acoustic Bass)

@violin1 = true
@violin2 = true
@viola = true
@continuo1 = true
@continuo2 = true

UI.pad(1, label: "Violin I", color: :red, type: :toggle) do |on|
  @violin1 = on
end

UI.pad(2, label: "Violin II", color: :yellow, type: :toggle) do |on|
  @violin2 = on
end

UI.pad(3, label: "Viola", color: :cyan, type: :toggle) do |on|
  @viola = on
end

UI.pad(4, label: "Continuo 1", color: :blue, type: :toggle) do |on|
  @continuo1 = on
end

UI.pad(5, label: "Continuo 2", color: :orange, type: :toggle) do |on|
  @continuo2 = on
end

# Create MML sequences
UI.log("Creating MML sequences...")
violin1_seq = MIDI::MML::Sequence.new(violin1_mml, channel: 0)
violin2_seq = MIDI::MML::Sequence.new(violin2_mml, channel: 1)
viola_seq = MIDI::MML::Sequence.new(viola_mml, channel: 2)
continuo1_seq = MIDI::MML::Sequence.new(continuo1_mml, channel: 3)
continuo2_seq = MIDI::MML::Sequence.new(continuo2_mml, channel: 4)

# Create MML players
UI.log("Creating MML players...")
violin1_player = MIDI::MML::Player.new(device, violin1_seq, loop: true)
violin2_player = MIDI::MML::Player.new(device, violin2_seq, loop: true)
viola_player = MIDI::MML::Player.new(device, viola_seq, loop: true)
continuo1_player = MIDI::MML::Player.new(device, continuo1_seq, loop: true)
continuo2_player = MIDI::MML::Player.new(device, continuo2_seq, loop: true)

# Main playback loop
on_loop = Proc.new { UI.process }
MIDI.bpm_loop(25, output: device, subdivisions: 24, on_loop: on_loop) do |clock|
  violin1_player.tick(clock) if @violin1
  violin2_player.tick(clock) if @violin2
  viola_player.tick(clock) if @viola
  continuo1_player.tick(clock) if @continuo1
  continuo2_player.tick(clock) if @continuo2
end
