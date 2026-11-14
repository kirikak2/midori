# UI mode (main_task_base.rb's UI branch, without the console and PicoModem):
# the startup script once (the page sets $sim_boot_autorun on the first start
# with the device's files), else the built-in devices and plugins, the
# Sequencer's song and the AMY panel, then UI.process until the page runs a
# script.
puts "Board: #{BoardConfig::BOARD_NAME}"
$midori_mode = :ui
if $sim_boot_autorun && File.file?(APP_SCRIPT)
  puts "Startup script: #{APP_SCRIPT}"
  sim_request_script(APP_SCRIPT)
  loop { MIDI.sleep_ms 100 }   # the page starts script mode
end
SDLoader.scan(PLUGIN_DIRS)
setup_midi_devices
setup_sequencer
setup_amy_panel
GC.start
UI._loading(nil, 0, 0)
puts "UI Mode: Waiting for script selection"
loop do
  UI.process
  # A layout's pads and knobs (the AMY panel) run their blocks here
  MIDI.sleep_ms(UI._requested_screen[0] == UI::SCREEN_LAYOUT ? 10 : 100)
end
