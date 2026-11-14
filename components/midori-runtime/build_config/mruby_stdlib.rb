# mruby's own standard library, shared by the device's gem set
# (picoruby_gems.rb) and the web simulator's build (sim/build_config/
# midori-sim.rb), so a script finds the same methods in both (Float#clamp
# is mruby-compar-ext). mruby/c has most of these built in, so scripts
# written for it expect them. eval'ed inside a MRuby::CrossBuild block:
# `conf` is the build.
mruby_gems = "#{MRUBY_ROOT}/mrbgems/picoruby-mruby/lib/mruby/mrbgems"   # MRUBY_ROOT: picoruby, in both builds
%w[
  mruby-kernel-ext mruby-object-ext mruby-string-ext mruby-symbol-ext
  mruby-array-ext mruby-hash-ext mruby-range-ext mruby-numeric-ext
  mruby-proc-ext mruby-enum-ext mruby-compar-ext mruby-toplevel-ext
  mruby-class-ext mruby-struct mruby-objectspace mruby-metaprog
  mruby-error mruby-sprintf mruby-math
].each { |g| conf.gem gemdir: "#{mruby_gems}/#{g}" }
