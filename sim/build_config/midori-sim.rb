# PicoRuby build for the MIDoRI simulator (sim/README.md, docs/WEB_SIMULATOR.md).
#
# picoruby-wasm's own build (picoruby/build_config/picoruby-wasm.rb) with the
# MIDoRI gems in place of the web app ones. Only libmruby.a is made here:
# sim/build.sh links it with sim/out/<board>/libmidori_sim.a (LovyanGFX,
# main/ui and the gems' ESP32 ports over the shims). The MIDoRI gems ship
# only ports/esp32, so with `ports :posix` rake compiles their bindings and
# mrblib but no port: that half comes from libmidori_sim.a.
#
# The defines that change mrb_state / mrb_value are picoruby-wasm's; nothing
# outside libmruby.a includes mruby headers, so they need not be repeated
# for the C half.
MIDORI_ROOT = File.expand_path('../..', __dir__)

MRuby::CrossBuild.new("midori-sim") do |conf|
  conf.toolchain :clang
  conf.ports :posix

  conf.cc.defines << "PICORB_PLATFORM_POSIX"
  conf.cc.defines << "PICORB_PLATFORM_WASM"
  conf.cc.defines << "MRB_TICK_UNIT=4"
  conf.cc.defines << "MRB_TIMESLICE_TICK_COUNT=1"
  conf.cc.defines << "MRB_32BIT"
  conf.cc.defines << "MRB_INT32"
  conf.cc.defines << "MRB_WORD_BOXING"
  conf.cc.defines << "MRB_UTF8_STRING"
  conf.cc.defines << "MRB_METHOD_CACHE_SIZE=1024"
  # The gems' C sees the simulator's MIDI build (picoruby-midi's polled input)
  conf.cc.defines << "MIDI_INPUT_POLLED=1"
  conf.cc.defines << "MIDORI_SIM=1"

  conf.cc.command = 'emcc'
  conf.linker.command = 'emcc'
  conf.archiver.command = 'emar'
  conf.cc.flags << "-fwasm-exceptions"

  conf.picoruby(alloc_estalloc: false)
  conf.aot

  conf.gembox "mruby-posix"
  conf.gembox "stdlib"
  conf.gem gemdir: "#{MRUBY_ROOT}/mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-math"
  conf.gem gemdir: "#{MRUBY_ROOT}/mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-bigint"
  conf.gem core: 'picoruby-wasm'

  midori_gems = "#{MIDORI_ROOT}/mrbgems"
  conf.gem "#{midori_gems}/picoruby-usb_midi_host"
  conf.gem "#{midori_gems}/picoruby-usb_midi_device"
  conf.gem "#{midori_gems}/picoruby-uart_midi"
  conf.gem "#{midori_gems}/picoruby-sam2695"
  conf.gem "#{midori_gems}/picoruby-midi"
  conf.gem "#{midori_gems}/picoruby-midi-mml"
  conf.gem "#{midori_gems}/picoruby-amy"        # its engine and port: build_c.sh (AMY_GEM_POLLED)
  conf.gem "#{midori_gems}/picoruby-ui"
  conf.gem "#{midori_gems}/picoruby-web_midi"   # MIDIDevices.web_midi (wasm only)
end
