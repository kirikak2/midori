# UI mode (main_task_base.rb's UI branch, without the SD card, the console
# and PicoModem): the built-in devices, the Sequencer's song and the AMY
# panel, then UI.process until the page runs a script.
puts "Board: #{BoardConfig::BOARD_NAME}"
$midori_mode = :ui
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
