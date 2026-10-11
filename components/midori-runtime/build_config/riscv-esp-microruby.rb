MRuby::CrossBuild.new('esp32-microruby') do |conf|
  conf.toolchain('gcc')

  conf.cc.command = 'riscv32-esp-elf-gcc'
  conf.linker.command = 'riscv32-esp-elf-ld'
  conf.archiver.command = 'riscv32-esp-elf-ar'

  conf.cc.host_command = 'gcc'
  # ESP32-P4 uses RISC-V with single-precision float ABI
  conf.cc.flags << '-march=rv32imafc_zicsr_zifencei'
  conf.cc.flags << '-mabi=ilp32f'
  conf.cc.flags << '-Wall'
  conf.cc.flags << '-Wno-format'
  conf.cc.flags << '-Wno-unused-function'
  conf.cc.flags << '-Wno-maybe-uninitialized'

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

  # AMY software synthesizer — midori-specific gem. ESP32-P4 boards only
  # (Tab5 / CrowPanel), so it is not in the xtensa (ESP32-S3) config.
  conf.gem File.expand_path('../../../mrbgems/picoruby-amy', __dir__)
end
