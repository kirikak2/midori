MRuby::Gem::Specification.new('picoruby-web_midi') do |spec|
  spec.license = 'MIT'
  spec.author  = 'Toshio Maki'
  spec.summary = 'Web MIDI ports as picoruby-midi transports (PicoRuby.wasm only)'
  spec.require_name = 'web_midi'
  spec.add_dependency 'picoruby-midi'
end
