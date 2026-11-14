
# After the script: back to UI mode, as on the device
puts "Script finished"
sim_script_ended
loop do
  UI.process
  MIDI.sleep_ms 50
end
