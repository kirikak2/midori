# Minimal reproducer for closure-access crash.
# No MIDI involvement. Just builds a Proc that captures a method-local
# variable and calls it.

require 'ui'

UI.log("closure_repro: start")

def make_handler(tag)
  Proc.new { puts "tag=#{tag}" }
end

UI.log("closure_repro: building proc")
h = make_handler("HELLO")
UI.log("closure_repro: calling proc")
h.call
UI.log("closure_repro: returned from first call")

# call several more times; if the first call doesn't crash, see if
# repeated calls do.
50.times do |i|
  h.call
end
UI.log("closure_repro: 50 calls done")

# now build many procs (mimic install_handlers' loop with 11+11 procs)
handlers = []
22.times do |i|
  handlers << make_handler("H#{i}")
end
UI.log("closure_repro: 22 procs built")
handlers.each { |p| p.call }
UI.log("closure_repro: 22 calls done - test PASSED")

# keep alive so we can read logs
loop { sleep_ms 1000 }
