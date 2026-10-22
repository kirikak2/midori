# UI::Tempo: the tempo, and a MIDI device that sends clock (docs/TEMPO.md)
#
#   UI::Tempo.bpm = 128
#   MIDI.route(UI::Tempo, MIDIDevices.usb_midi_device)   # clock to the PC
#   UI::Tempo.start                                       # MIDI Start
#
#   # The BPM from a knob, following TAP / Sync too
#   UI.knob(1, label: "BPM", min: 40, max: 240, step: 1, value: UI::Tempo.bpm) do |v|
#     UI::Tempo.bpm = v
#   end
#   UI::Tempo.on_change { |bpm| UI.knob_set(1, bpm, notify: false) }
#   UI.pad(1, label: "TAP") { UI::Tempo.tap }
#
# The clock (24 per quarter note) runs at the current BPM -- the external
# clock's while Sync is on -- whenever something is routed from the tempo,
# in C: what the script does in between does not move it. It is a MIDI
# device that only sends, so it has no MIDI OUT column on the MIDI Devices
# screen. In a layout: `tempo area: :t`.
module UI
  class Tempo
    class << self
      # @return [Float] the tempo the clock runs at (the external clock's in Sync)
      def bpm
        UI._bpm
      end

      # The internal tempo (20..300). No effect on the clock while Sync is on.
      def bpm=(value)
        UI._set_bpm(value.to_f)
      end

      # One tap of TAP tempo (as the screen's TAP)
      def tap
        UI._tempo_tap
        nil
      end

      # MIDI Start / Stop to wherever the tempo is routed
      def start
        UI._tempo_set_running(true)
        nil
      end

      def stop
        UI._tempo_set_running(false)
        nil
      end

      # Between start and stop
      def running?
        UI._tempo_running
      end

      # Following the external clock (the screen's Sync)
      def sync?
        UI._tempo_sync
      end

      # Turning it on takes only while an external clock is there
      def sync=(on)
        UI._tempo_set_sync(on ? true : false)
      end

      # The block gets the new BPM whenever it changes (TAP, Sync, a script)
      def on_change(&block)
        UI.on(:bpm_change) { |e| block.call(e[:bpm]) }
        block
      end

      def show
        UI.show(:tempo)
      end

      # --- Transport (picoruby-midi): MIDI.route(UI::Tempo, dev) ---

      # Its bit in the transport registry (0 on a board without a display)
      def transport_id
        UI._tempo_bit
      end

      # Nothing is sent to the tempo
      def send_packet(cable, cin, midi1, midi2, midi3)
        0
      end

      def bytes_available
        0
      end

      def read_available
        nil
      end

      def connected?
        transport_id != 0
      end

      def device_info
        "Tempo (MIDI clock)"
      end
    end
  end
end
