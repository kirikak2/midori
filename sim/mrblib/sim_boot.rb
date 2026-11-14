# The simulator's start-up, after BoardConfig and the shared part of
# main_task_base.rb (sim/build.sh concatenates them into sim_boot-<board>.rb).
# The page then appends sim_ui.rb (UI mode) or sim_script.rb + the user's
# script + sim_tail.rb (script mode), as the device's main_task.rb branches.

# The files of the connected MIDoRI (the page copies /plugins, /sd/plugins and
# /app.rb into Emscripten's FS before the VM starts), read by the device's own
# SDLoader: none without a device, so the volumes count as there and the
# directories as empty.
$flash_available = true
$sd_available = true

# setup_midi_devices calls this between the built-ins and the saved patches:
# the page's Web MIDI ports first, so plugins and saved patches find them,
# then the plugins as on the device (/plugins, then /sd/plugins)
alias device_load_plugins load_plugins
def load_plugins
  WebMIDI.register_devices
  device_load_plugins
end

# Ask the page to run a script (the startup script): it starts script mode in
# a fresh VM with the file's text, as the device's Supervisor does
def sim_request_script(path)
  JS.global[:__midoriSim].requestScript(path)
rescue => e
  puts "[sim] #{e.message}"
end

# Tell the page (sim/web/device.js) that the script ended: it starts UI mode
# in a fresh VM, as the device's Supervisor does
def sim_script_ended
  JS.global[:__midoriSim].scriptEnded
rescue => e
  puts "[sim] #{e.message}"
end

# ScriptManager is midori-runtime's C class on the device (picoruby-esp32.c).
# Scripts use it to stop when asked; here the page stops a script by
# throwing its VM away, so it is never asked.
unless Object.const_defined?(:ScriptManager)
  class ScriptManager
    def stop_requested?; false; end
    def get_requested; nil; end
    def clear_request; nil; end
    def check_console; nil; end
    def free_heap; 0; end
  end
end

# Live patching from the designer (docs/WEB_SIMULATOR.md §6.3): Ruby the page
# queues (__midoriSim.takeLive) runs between UI.process passes, never inside
# a handler, as the AMY panel's redefinitions do. An error tells the page,
# which starts the preview again.
module UI
  class << self
    alias sim_process_without_live process
    def process(*args)
      result = sim_process_without_live(*args)
      sim_live_poll
      result
    end
  end
end

def sim_live_poll
  bridge = JS.global[:__midoriSim]
  # Asked once: a page whose bridge has no takeLive is never called for it
  $sim_live_ok = !bridge[:takeLive].nil? if $sim_live_ok.nil?
  return unless $sim_live_ok
  code = bridge.takeLive
  return unless code.is_a?(String)
  begin
    eval(code)
  rescue Exception => e
    puts "[live] #{e.class}: #{e.message}"
    bridge.liveFailed("#{e.class}: #{e.message}")
  end
rescue => e
  # A page without live patching (the simulator's own): nothing to take
end
