
# After the script: as on the device, the screen stays and handlers keep running
puts "Script finished"
loop do
  UI.process
  MIDI.sleep_ms 50
end
