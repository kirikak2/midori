# Script mode (main_task_base.rb's script branch): the built-in devices and
# the Sequencer, then the user's script (appended by the page)
$midori_mode = :script
setup_midi_devices
setup_sequencer
UI._loading(nil, 0, 0)
