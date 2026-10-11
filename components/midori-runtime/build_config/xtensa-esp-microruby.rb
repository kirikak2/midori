MRuby::CrossBuild.new('esp32-microruby') do |conf|
  conf.toolchain('gcc')

  conf.cc.command = 'xtensa-esp32-elf-gcc'
  conf.linker.command = 'xtensa-esp32-elf-ld'
  conf.archiver.command = 'xtensa-esp32-elf-ar'

  conf.cc.host_command = 'gcc'
  conf.cc.flags << '-Wall'
  conf.cc.flags << '-Wno-format'
  conf.cc.flags << '-Wno-unused-function'
  conf.cc.flags << '-Wno-maybe-uninitialized'
  conf.cc.flags << '-mlongcalls'

  # Every define that changes the layout of mrb_state / mrb_value must also
  # be passed to the IDF side (ADDITIONAL_DEFINITIONS in CMakeLists.txt).
  conf.cc.defines << 'MRB_TICK_UNIT=10'
  conf.cc.defines << 'MRB_TIMESLICE_TICK_COUNT=1'
  conf.cc.defines << 'MRB_INT64'
  conf.cc.defines << 'MRB_NO_BOXING'
  conf.cc.defines << 'MRB_32BIT'
  # picoruby-mruby adds this one itself, but only after its own sources (the
  # mruby core) are set up, so the core would be built without it and the
  # ext gems with it. Same as the upstream R2P2 configs.
  conf.cc.defines << 'MRB_UTF8_STRING'
  conf.cc.defines << 'PICORB_ALLOC_ESTALLOC'
  conf.cc.defines << 'PICORB_ALLOC_ALIGN=8'
  conf.cc.defines << 'USE_FAT_FLASH_DISK'
  conf.cc.defines << 'USE_FAT_SD_DISK'
  conf.cc.defines << 'ESP32_PLATFORM'
  conf.cc.defines << 'NDEBUG'

  if ENV['PICORUBY_DEBUG']
    conf.cc.defines << 'ESTALLOC_DEBUG'
    conf.enable_debug
  end

  conf.picoruby

  eval File.read(File.expand_path('microruby_gems.rb', __dir__))
end
