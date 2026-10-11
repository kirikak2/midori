# Gem set shared by xtensa-esp-microruby.rb and riscv-esp-microruby.rb
# (eval'ed inside their MRuby::CrossBuild block, so `conf` is the build).

# Pull in FAT BEFORE the gemboxes so that gembox-time conditionals
# (`if gems.any? { picoruby-filesystem-fat }`) see it and skip pulling
# picoruby-littlefs as a dependency.
conf.gem core: 'picoruby-filesystem-fat'
conf.gembox 'minimum'
conf.gembox 'core'
# The 'shell' gembox minus picoruby-rapicco. mruby runs every gem's mrblib
# when the VM opens (mruby/c only on require), and rapicco's starts with
# `require 'karmatic_arcade'`, a font gem this build does not have: the
# LoadError aborts mrb_open() and leaves every gem after it uninitialised.
conf.gem core: 'picoruby-shell'
conf.gem core: 'picoruby-picoline'
conf.gem core: 'picoruby-vim'

# mruby's own standard library. mruby/c has most of these methods built in,
# so scripts written for it expect them.
mruby_gems = File.expand_path('../picoruby/mrbgems/picoruby-mruby/lib/mruby/mrbgems', __dir__)
%w[
  mruby-kernel-ext mruby-object-ext mruby-string-ext mruby-symbol-ext
  mruby-array-ext mruby-hash-ext mruby-range-ext mruby-numeric-ext
  mruby-proc-ext mruby-enum-ext mruby-compar-ext mruby-toplevel-ext
  mruby-class-ext mruby-struct mruby-objectspace mruby-metaprog
  mruby-error mruby-sprintf mruby-math
].each { |g| conf.gem gemdir: "#{mruby_gems}/#{g}" }

# stdlib
conf.gem core: 'picoruby-rng'
conf.gem core: 'picoruby-base64'
conf.gem core: 'picoruby-yaml'

# peripherals
conf.gem core: 'picoruby-gpio'
conf.gem core: 'picoruby-i2c'
conf.gem core: 'picoruby-spi'
conf.gem core: 'picoruby-sdmmc'
conf.gem core: 'picoruby-adc'
conf.gem core: 'picoruby-uart'
conf.gem core: 'picoruby-pwm'

# others
conf.gem core: 'picoruby-esp32'
conf.gem core: 'picoruby-rmt'
conf.gem core: 'picoruby-mbedtls'
conf.gem core: 'picoruby-socket'
conf.gem core: 'picoruby-adafruit_sk6812'

midori_gems = File.expand_path('../../../mrbgems', __dir__)

# MIDI — midori-specific gems, live outside the picoruby submodule.
conf.gem "#{midori_gems}/picoruby-usb_midi_host"
conf.gem "#{midori_gems}/picoruby-usb_midi_device"
conf.gem "#{midori_gems}/picoruby-uart_midi"
conf.gem "#{midori_gems}/picoruby-sam2695"
conf.gem "#{midori_gems}/picoruby-midi"
conf.gem "#{midori_gems}/picoruby-midi-mml"

# UI (M5Stack) — midori-specific gem, lives outside the picoruby submodule.
conf.gem "#{midori_gems}/picoruby-ui"

# DFRobot Visual Rotary Encoder (SEN0502) — pure Ruby on top of picoruby-i2c.
conf.gem "#{midori_gems}/picoruby-dfrobot_rotary_encoder"
