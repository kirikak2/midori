# The simulator's start-up, after BoardConfig and the shared part of
# main_task_base.rb (sim/build.sh concatenates them into sim_boot.rb).
# The page appends the user's script and sim_tail.rb.

$midori_mode = :script

# setup_midi_devices calls this between the built-ins and the saved patches.
# No /sd/plugins yet (the virtual /sd comes later); the page's Web MIDI ports
# are listed here, so patches saved to them are routed again.
def load_plugins
  WebMIDI.register_devices
end

setup_midi_devices
setup_sequencer
UI._loading(nil, 0, 0)
