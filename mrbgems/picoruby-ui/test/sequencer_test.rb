# Host tests for UI::Sequencer (mrblib/ui_sequencer.rb). The C side
# (ui_sequencer.cpp) is a Ruby stand-in that records what it is told; the
# routes go through picoruby-midi's real MIDI.route on its C stubs. Playback
# itself is tested in C: make -C main/ui/host_test.
#
#   ruby test/sequencer_test.rb          (from the picoruby-ui directory)

midi = File.expand_path("../../picoruby-midi", File.dirname(__FILE__))
load File.join(midi, "host_test/filter_stubs.rb")
load File.join(midi, "mrblib/midi_constants.rb")
load File.join(midi, "mrblib/midi_device.rb")
load File.join(midi, "mrblib/midi_filter.rb")
load File.join(midi, "mrblib/midi_route.rb")

SEQ_BIT = 0x40
TEMPO_BIT = 0x08

module MIDI
  class << self
    def _route_sources; 0x07 | TEMPO_BIT | SEQ_BIT; end
  end
end

# A transport stand-in
class FakeTransport
  attr_reader :transport_id
  def initialize(bit); @transport_id = bit; end
end

$seq = nil

def reset_stub
  $stub[:routes] = []
  $seq = {
    lines: 8, sets: [], notes: {}, chord: [[60, 100]], chain: [], names: {}, resets: [],
    values: {}, song: "", edits: 1, saved_edits: 1, autosaved_edits: 1, dump_ends: [],
    commits: [], requests: [], due: false
  }
end

module UI
  module Tempo
    def self.transport_id; TEMPO_BIT; end
    def self.send_packet(*a); 0; end
  end

  class Tombola
    def self.transport_mask_for(d)
      d.respond_to?(:transport_id) ? d.transport_id : 0
    end
  end

  class << self
    def _seq_reset(lines)
      $seq[:resets] << lines
      $seq[:lines] = lines
    end

    def _seq_set_i(pattern, line, name, value)
      # Like the engine: only a realtime line can be armed
      return false if name == :armed && value >= 0 && $seq[:values][[:mode, value]] != 1
      $seq[:sets] << [pattern, line, name, value]
      $seq[:values][[name, line]] = value
      true
    end

    def _seq_get_i(pattern, line, name)
      case name
      when :transport_bit then SEQ_BIT
      when :line_count then $seq[:lines]
      when :play_pattern then 3
      when :next_pattern, :edit_pattern then -1
      when :armed then $seq[:values][[:armed, -1]] || -1
      when :pattern_tick then 384 + 96 * 2 + 10
      when :gate then $seq[:values][[:gate, line]] || 100
      when :program, :bank_msb, :bank_lsb then $seq[:values][[name, line]] || -1
      else $seq[:values][[name, line]] || 0
      end
    end

    def _seq_set_name(line, name); $seq[:names][line] = name; true; end
    def _seq_name(line); $seq[:names][line] || ""; end

    def _seq_add_line
      return -1 if $seq[:lines] >= 16
      $seq[:lines] += 1
      $seq[:lines] - 1
    end

    def _seq_remove_line(i); $seq[:lines] -= 1; true; end

    def _seq_place(p, l, step, len, notes, vels)
      list = ($seq[:notes][[p, l, 0]] ||= [])
      notes.each_with_index { |n, i| list << [step, len, n, vels[i]] }
      list.sort!
      notes.size
    end

    def _seq_remove(p, l, step, note)
      list = ($seq[:notes][[p, l, 0]] ||= [])
      before = list.size
      list.delete_if { |pos, _len, n, _v| pos == step && (note < 0 || n == note) }
      before - list.size
    end

    def _seq_set_length(p, l, step, len); true; end

    def _seq_add_event(p, l, pos, len, note, vel)
      ($seq[:notes][[p, l, 1]] ||= []) << [pos, len, note, vel]
      true
    end

    def _seq_clear(p, l, mode); $seq[:notes].delete([p, l, mode]); end
    def _seq_notes(p, l, mode); ($seq[:notes][[p, l, mode]] || []).dup; end
    def _seq_chord; $seq[:chord]; end
    def _seq_set_chord(notes, vels); $seq[:chord] = notes.zip(vels); end
    def _seq_chain; $seq[:chain]; end
    def _seq_set_chain(pats, reps); $seq[:chain] = pats.zip(reps); true; end
    def _seq_clear_pattern(p); true; end
    def _seq_copy_pattern(s, d); $seq[:copied] = [s, d]; true; end
    def _seq_copy_line(sp, sl, dp, dl); $seq[:line_copied] = [sp, sl, dp, dl]; true; end
    def _seq_receive(*a); $seq[:received] = a; end

    # Files: the "song" written is $seq[:song], a String, in two pieces
    def _seq_dump_begin(dirty)
      return false if $seq[:dump_fail]
      $seq[:dump] = ["MSEQ" + (dirty ? "D" : "C"), $seq[:song].to_s]
      $seq[:dump_edits] = $seq[:edits]
      true
    end
    def _seq_dump_next; $seq[:dump].shift; end
    def _seq_dump_end(saved, autosave)
      $seq[:saved_edits] = $seq[:dump_edits] if saved
      $seq[:autosaved_edits] = $seq[:dump_edits] if autosave
      $seq[:dump_ends] << [saved, autosave]
    end
    def _seq_load_begin; $seq[:loading] = ""; true; end
    def _seq_load_feed(str)
      $seq[:loading] << str
      $seq[:loading].start_with?("MSEQ") || $seq[:loading].size < 4 ? 0 : -1
    end
    def _seq_load_commit(autosave)
      $seq[:song] = $seq[:loading][5..-1]
      $seq[:commits] << autosave
      $seq[:edits] += 1
      $seq[:saved_edits] = $seq[:edits]
      0
    end
    def _seq_load_abort; $seq[:loading] = nil; end
    def _seq_load_error(code); "error #{code}"; end
    def _seq_take_request; $seq[:requests].shift; end
    def _seq_set_files(names); $seq[:files] = names; end
    def _seq_status(msg); $seq[:status] = msg; end
  end
end

# The C state the save code reads, on top of _seq_get_i's table
module UI
  class << self
    alias_method :_seq_get_i_base, :_seq_get_i
    def _seq_get_i(pattern, line, name)
      case name
      when :dirty then $seq[:edits] != $seq[:saved_edits] ? 1 : 0
      when :autosave_due then $seq[:due] ? 1 : 0
      when :temporary, :restored, :bpm_x100 then $seq[:values][[name, -1]] || 0
      else _seq_get_i_base(pattern, line, name)
      end
    end
  end
end

load File.join(File.dirname(__FILE__), "../mrblib/ui_sequencer.rb")

$failures = 0
def check(name, cond)
  unless cond
    puts "FAIL: #{name}"
    $failures += 1
  end
end

def raises?
  yield
  false
rescue ArgumentError
  true
end

def set_of(name, line = nil)
  $seq[:sets].reverse.find { |s| s[2] == name && (line.nil? || s[1] == line) }
end

kb = FakeTransport.new(0x01)
synth = FakeTransport.new(0x10)
amy = FakeTransport.new(0x20)

# --- new: reset, the Tempo clock routed alone ---
reset_stub
seq = UI::Sequencer.new(device: synth)
check("reset with 8 lines", $seq[:resets] == [8])
check("out_mask", set_of(:out_mask)[3] == 0x10)
check("clock route from Tempo", $stub[:routes].size == 1)
r = $stub[:routes][0]
check("clock route: Tempo -> seq", r[0] == TEMPO_BIT && r[1] == SEQ_BIT)
check("clock route carries the clock only", r[3] == MIDI::ROUTE_SEL_CLOCK)
check("transport", seq.transport_id == SEQ_BIT && seq.connected?)
check("current is the instance", UI::Sequencer.current.equal?(seq))

# --- inputs get channels only; a source that is both gets one route ---
reset_stub
seq = UI::Sequencer.new(lines: 4, input: kb)
check("4 lines", $seq[:resets] == [4])
check("two routes", $stub[:routes].size == 2)
in_route = $stub[:routes].find { |x| x[0] == 0x01 }
check("input route: channels, no clock", in_route[3] == 0xFFFF)

reset_stub
seq = UI::Sequencer.new(clock: kb, input: [kb])
check("one route for clock + notes", $stub[:routes].size == 1)
check("... carrying both", $stub[:routes][0][3] == (0xFFFF | MIDI::ROUTE_SEL_CLOCK))

reset_stub
seq = UI::Sequencer.new(clock: nil)
check("clock: nil routes nothing", $stub[:routes].empty?)
seq.clock = kb
check("clock= routes the new clock", $stub[:routes].size == 1 && $stub[:routes][0][0] == 0x01)
seq.clock = UI::Tempo
check("clock= drops the old route", $stub[:routes].size == 1 && $stub[:routes][0][0] == TEMPO_BIT)
seq.input = kb
check("input= adds a route", $stub[:routes].size == 2)
seq.input = nil
check("input = nil removes it", $stub[:routes].size == 1)

check("bad lines", raises? { UI::Sequencer.new(lines: 0) })
check("bad lines (17)", raises? { UI::Sequencer.new(lines: 17) })
check("bad view", raises? { UI::Sequencer.new(view: 12) })

# --- current: nothing reset, nothing routed ---
reset_stub
$ui_sequencer_instance = nil
cur = UI::Sequencer.current
check("current: no reset", $seq[:resets].empty?)
check("current: no routes", $stub[:routes].empty?)

# --- line settings ---
reset_stub
seq = UI::Sequencer.new(clock: nil)
l = seq.line(1, name: "Bass", channel: 2, steps: 32, rate: :"16t", gate: 80, mode: :grid)
check("name", $seq[:names][1] == "Bass")
check("channel", set_of(:channel, 1)[3] == 2)
check("steps", set_of(:steps, 1)[3] == 32)
check("rate :16t", set_of(:rate, 1)[3] == 5)
check("gate", set_of(:gate, 1)[3] == 80)
check("line of the edit pattern", set_of(:steps, 1)[0] == UI::Sequencer::EDIT)
l.quantize = 16
check("quantize 16 -> rate code + 1", set_of(:quantize, 1)[3] == 3)
l.quantize = nil
check("quantize nil -> 0", set_of(:quantize, 1)[3] == 0)
l.velocity = :input
check("velocity :input -> 0", set_of(:velocity, 1)[3] == 0)
l.mode = :realtime
check("mode realtime", set_of(:mode, 1)[3] == 1 && l.mode == :realtime)
l.mode = :grid
check("bad channel", raises? { l.channel = 16 })
check("bad steps", raises? { l.steps = 12 })
check("bad rate", raises? { l.rate = 7 })
check("bad gate", raises? { l.gate = 0 })
check("bad mode", raises? { l.mode = :step })
check("bad line index", raises? { seq.line(8) })
check("pattern line", seq.pattern(5).line(0).pattern_index == 5)
seq.pattern(5).line(0, bars: 4)
check("pattern line bars", set_of(:bars, 0)[0] == 5 && set_of(:bars, 0)[3] == 4)
check("bad pattern", raises? { seq.pattern(16) })

# --- program change ---
pl = seq.pattern(2).line(1)
check("no program by default", pl.program.nil? && pl.bank.nil?)
pl.program = 81
check("program", set_of(:program, 1)[3] == 81 && set_of(:program, 1)[0] == 2 && pl.program == 81)
pl.program = nil
check("program nil -> -1", set_of(:program, 1)[3] == -1)
pl.bank = 3
check("bank msb only", set_of(:bank_msb, 1)[3] == 3 && set_of(:bank_lsb, 1)[3] == -1)
check("bank reads back", pl.bank == [3, nil])
pl.bank = [1, 2]
check("bank msb / lsb", pl.bank == [1, 2])
pl.bank = nil
check("bank nil", pl.bank.nil?)
check("bad program", raises? { pl.program = 128 })
check("bad bank", raises? { pl.bank = [0, 200] })
seq.pattern(2).line(1, program: 5, bank: [0, 1])
check("configure program / bank", pl.program == 5 && pl.bank == [0, 1])

# --- grid notes ---
reset_stub
seq = UI::Sequencer.new(clock: nil)
l = seq.line(0)
l[0] = [60, 64, 67]
check("chord placed", l[0].map { |n| n[:note] } == [60, 64, 67])
check("velocity 100 by default", l[0].all? { |n| n[:velocity] == 100 })
l[0] = 48
check("[]= replaces", l[0].map { |n| n[:note] } == [48])
l.velocity = 90
$seq[:values][[:velocity, 0]] = 90
l.add(4, note: 50, length: 2)
check("add uses the line's velocity", l[4] == [{ note: 50, length: 2, velocity: 90 }])
l.set(4, notes: [52], length: 3, velocity: 70)
check("set replaces with given velocity", l[4] == [{ note: 52, length: 3, velocity: 70 }])
l[4] = nil
check("[]= nil clears", l[4] == [])
check("bad step", raises? { l[64] = 60 })

reset_stub
seq = UI::Sequencer.new(clock: nil)
l = seq.line(0)
l.pattern("x...x-..|x..X--.x", notes: [36])
starts = l.notes.map { |n| [n[:pos], n[:len], n[:note]] }
check("pattern string", starts == [[0, 1, 36], [4, 2, 36], [8, 1, 36], [11, 3, 36], [15, 1, 36]])
seq.chord = [[60, 90], 64]
l.pattern("x.......")
check("pattern uses the chord", l.notes.map { |n| n[:note] } == [60, 64])

# --- realtime notes ---
$seq[:values][[:mode, 2]] = 1
r = seq.line(2)
r.add_event(pos: 13, len: 50, note: 72)
check("realtime notes", r.notes == [{ pos: 13, len: 50, note: 72, velocity: 100 }])
r.notes = [{ pos: 0, len: 96, note: 60 }]
check("notes= replaces", r.notes.map { |n| n[:pos] } == [0])

# --- recording ---
check("nothing armed", seq.armed.nil? && !r.armed?)
check("arm returns the line", r.arm.equal?(r))
check("armed", r.armed? && seq.armed.index == 2 && $seq[:values][[:armed, -1]] == 2)
check("arm a grid line raises", raises? { seq.line(0).arm })
check("still armed after a refused arm", r.armed?)
seq.line(0).disarm
check("disarm of a line not armed does nothing", r.armed?)
r.disarm
check("disarm", !r.armed? && seq.armed.nil?)
r.arm
seq.disarm
check("seq.disarm", seq.armed.nil?)
r.configure(quantize: 16, rec_mode: :replace)
check("quantize / rec_mode", $seq[:values][[:quantize, 2]] == 3 && $seq[:values][[:rec_mode, 2]] == 1)

# --- chord ---
seq.chord = 60
check("chord one note", seq.chord == [60])
seq.chord = [[60, 90], [64, 80]]
check("chord pairs", seq.chord_with_velocities == [[60, 90], [64, 80]])
check("chord too big", raises? { seq.chord = (1..9).to_a })

# --- patterns and chain ---
seq.play_pattern = 2
check("play_pattern", set_of(:play_pattern)[3] == 2)
check("playing_pattern", seq.playing_pattern == 3)
check("next_pattern nil", seq.next_pattern.nil?)
seq.switch = :bar
check("switch bar", set_of(:switch)[3] == 1)
check("bad switch", raises? { seq.switch = :beat })
seq.edit_pattern = nil
check("edit_pattern nil -> -1", set_of(:edit_pattern)[3] == -1)
seq.chain = [[0, 2], 1, [3, 4]]
check("chain", seq.chain == [[0, 2], [1, 1], [3, 4]])
check("bad chain repeat", raises? { seq.chain = [[0, 17]] })
check("bad chain pattern", raises? { seq.chain = [16] })
seq.chain_on = true
check("chain_on", set_of(:chain_on)[3] == 1)
seq.pattern(1).copy_to(4)
check("copy_to", $seq[:copied] == [1, 4])
seq.pattern(1).length = :auto
check("length auto", set_of(:length)[3] == 0)
check("bad length", raises? { seq.pattern(1).length = 9 })

seq.pattern(1).line(2).copy_to(3)
check("line copy_to: same pattern", $seq[:line_copied] == [1, 2, 1, 3])
seq.line(0).copy_to(1, pattern: 4)
check("line copy_to: another pattern", $seq[:line_copied] == [UI::Sequencer::EDIT, 0, 4, 1])
check("line copy_to: bad line", raises? { seq.line(0).copy_to(99) })

# --- lines ---
reset_stub
seq = UI::Sequencer.new(clock: nil)
nl = seq.add_line(name: "Pad", channel: 3)
check("add_line", nl.index == 8 && $seq[:names][8] == "Pad")
7.times { seq.add_line }
check("16 lines", seq.lines == 16)
check("add_line at 16 -> false", seq.add_line == false)

# --- position ---
pos = seq.position
check("position", pos == { pattern: 3, tick: 586, bar: 1, beat: 2 })

# --- transport ---
seq.send_packet(0, 0xF, 0xF8, 0, 0)
check("send_packet", $seq[:received] == [0xF, 0xF8, 0, 0])

# --- Saving ---
require "tmpdir"
$tmp = Dir.mktmpdir("seqtest")
class << UI::Sequencer
  def dir; $tmp + "/patterns"; end
end
module MIDI
  def self._uptime_ms; $now_ms || 0; end
end
module UI
  module Tempo
    def self.bpm; 123.5; end
    def self.bpm=(v); $tempo_set = v; end
  end
end
S = UI::Sequencer
def file(name); File.join(S.dir, name); end

reset_stub
seq = S.new(clock: nil)
check("new marks the song temporary", $seq[:values][[:temporary, -1]] == 1)
S.new(clock: nil, autosave: true)
check("autosave: true keeps it the user's", $seq[:values][[:temporary, -1]] == 0)

$seq[:song] = "first"
check("save returns the name", seq.save("groove") == "groove")
check("file written", File.read(file("groove.mseq")) == "MSEQCfirst")
check("no .tmp / .bak left", !File.exist?(file("groove.mseq.tmp")) && !File.exist?(file("groove.mseq.bak")))
check("name set before writing", $seq[:names][-1] == "groove")
check("tempo stored", $seq[:values][[:bpm_x100, -1]] == 12350)
check("marked saved, not autosaved", $seq[:dump_ends].last == [true, false])
$seq[:song] = "second"
seq.save("groove")
check("overwrite", File.read(file("groove.mseq")) == "MSEQCsecond")

$seq[:song] = "x"
seq.load("groove", tempo: true)
check("load", $seq[:song] == "second" && $seq[:commits].last == false)
check("load tempo", $tempo_set == 123.5)
# A save cut off between the renames: only the .bak is there
File.rename(file("groove.mseq"), file("groove.mseq.bak"))
seq.load("groove")
check("falls back to .bak", $seq[:song] == "second")
File.rename(file("groove.mseq.bak"), file("groove.mseq"))
File.write(file("junk.mseq"), "JUNKJUNK")
check("bad file raises", (begin; seq.load("junk"); false; rescue IOError => e; e.message.include?("error"); end))
check("missing file raises", (begin; seq.load("nope"); false; rescue IOError; true; end))
check("bad name", raises? { seq.save("bad name") })
check("reserved name", raises? { seq.save("_x") })
$seq[:names][-1] = ""
check("no name: SONG001", seq.save == "SONG001")
check("then SONG002", S.next_auto_name == "SONG002")
File.write(file("_autosave.mseq"), "MSEQDauto")
check("songs: sorted, no _ files", S.songs == ["SONG001", "groove", "junk"])
$seq[:dump_fail] = true
check("failed write -> IOError", (begin; seq.save("other"); false; rescue IOError; true; end))
$seq[:dump_fail] = false

# Autosave: when C says it is due, with the dirty flag; retried later on failure
$seq[:due] = true
$seq[:edits] = 50
S._poll
check("autosave written", File.read(file("_autosave.mseq")) == "MSEQD" + $seq[:song])
check("marked autosaved only", $seq[:dump_ends].last == [false, true])
$seq[:dump_fail] = true
$now_ms = 1000
n = $seq[:dump_ends].size
S._poll
check("failure reported", $seq[:status] == "Autosave failed")
$seq[:dump_fail] = false
$now_ms = 5000
S._poll
check("no retry before 10 s", $seq[:dump_ends].size == n)
$now_ms = 12000
S._poll
check("retried after 10 s", $seq[:dump_ends].size == n + 1 && $seq[:dump_ends].last == [false, true])
$seq[:due] = false

# Start-up: the autosave once, and again after a temporary song
$seq[:values][[:restored, -1]] = 0
$seq[:song] = "script"
$seq[:values][[:transport_bit, -1]] = nil
S._setup
check("restored at start-up", $seq[:commits].last == true && $seq[:song] != "script")
check("restored flag set", $seq[:values][[:restored, -1]] == 1)
check("file list sent", $seq[:files].split("\n").include?("groove"))
c = $seq[:commits].size
S._setup
check("not again", $seq[:commits].size == c)
$seq[:values][[:temporary, -1]] = 1
S._setup
check("again after a temporary song", $seq[:commits].size == c + 1)
File.unlink(file("_autosave.mseq"))
$seq[:values][[:temporary, -1]] = 1
r = $seq[:resets].size
S._setup
check("no autosave: an empty song", $seq[:resets].size == r + 1 && $seq[:values][[:mark_saved, -1]] == 1)
check("... no longer temporary", $seq[:values][[:temporary, -1]] == 0)

# Requests from the File sheet
$seq[:requests] = [[S::REQ_SAVE, "live"]]
S._poll
check("request save", File.exist?(file("live.mseq")) && $seq[:status] == "Saved live")
$seq[:requests] = [[S::REQ_SAVE, ""]]
S._poll
check("request save without a name", $seq[:status] == "Saved SONG002")
$seq[:requests] = [[S::REQ_SAVE_NEW, "live"]]
S._poll
check("request save as new", $seq[:status] == "Saved SONG003")
$seq[:requests] = [[S::REQ_LOAD, "groove"]]
S._poll
check("request load", $seq[:status] == "Loaded groove")
$seq[:requests] = [[S::REQ_DELETE, "junk"]]
S._poll
check("request delete", !File.exist?(file("junk.mseq")) && !$seq[:files].include?("junk"))
$seq[:requests] = [[S::REQ_LOAD, "junk"]]
S._poll
check("errors become the status", $seq[:status].include?("not found"))
r = $seq[:resets].size
$seq[:requests] = [[S::REQ_NEW, ""]]
S._poll
check("request new", $seq[:resets].size == r + 1 && $seq[:values][[:mark_clean, -1]] == 1)

# to_h / restore
reset_stub
seq = S.new(clock: nil, lines: 2)
seq.name = "keep"
seq.line(0, name: "Kick", channel: 9).pattern("x...x...", notes: [36])
h = seq.to_h
check("to_h lines", h[:lines].size == 2 && h[:lines][0][:name] == "Kick")
reset_stub
seq = S.new(clock: nil)
seq.restore(h)
check("restore resets to the saved line count", $seq[:resets].last == 2)
check("restore name", $seq[:names][-1] == "keep")

FileUtils.rm_rf($tmp) if defined?(FileUtils)

if $failures == 0
  puts "all sequencer tests passed"
else
  puts "#{$failures} failure(s)"
  exit 1
end
