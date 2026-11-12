# The simulator's start-up, after BoardConfig and the shared part of
# main_task_base.rb (sim/build.sh concatenates them into sim_boot.rb).
# The page appends the user's script and sim_tail.rb.

$midori_mode = :script

# No /sd/plugins yet (the virtual /sd comes later)
def load_plugins; end

setup_midi_devices
setup_sequencer
UI._loading(nil, 0, 0)
