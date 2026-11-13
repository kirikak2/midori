# The simulator's start-up, after BoardConfig and the shared part of
# main_task_base.rb (sim/build.sh concatenates them into sim_boot-<board>.rb).
# The page then appends sim_ui.rb (UI mode) or sim_script.rb + the user's
# script + sim_tail.rb (script mode), as the device's main_task.rb branches.

# setup_midi_devices calls this between the built-ins and the saved patches.
# No /sd/plugins yet (the virtual /sd comes later); the page's Web MIDI ports
# are listed here, so patches saved to them are routed again.
def load_plugins
  WebMIDI.register_devices
end

# Tell the page (sim/web/device.js) that the script ended: it starts UI mode
# in a fresh VM, as the device's Supervisor does
def sim_script_ended
  JS.global[:__midoriSim].scriptEnded
rescue => e
  puts "[sim] #{e.message}"
end
