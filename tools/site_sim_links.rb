#!/usr/bin/env ruby
# Adds "open in the simulator" links to the reference pages' code cards
# (site/reference/index.html, site/ja/reference/index.html).
#
#   ruby tools/site_sim_links.rb [--dump DIR]
#
# Each card's Ruby goes into a share link of the simulator (site/simulator/,
# docs/WEB_SIMULATOR.md §15: #code=<base64url(deflate-raw)>), with a few lines
# around it so the fragment runs: `dev` / `fx` play the built-in synth, and a
# UI.process loop keeps it alive. Cards that cannot run there are left
# without a link: "..." placeholders, BLE (the simulator has none).
# Running it again replaces the links (the pages stay as they are otherwise).
# --dump writes each linked script to DIR (sim/test/node_reference.mjs runs
# them all in the simulator).
require "zlib"
require "base64"
require "cgi"
require "fileutils"

ROOT = File.expand_path("..", __dir__)
PAGES = {
  "site/reference/index.html" => { href: "../simulator/", lang: "en", label: "Open in the simulator ▶" },
  "site/ja/reference/index.html" => { href: "../../simulator/", lang: "ja", label: "シミュレーターで開く ▶" },
}
PRELUDE = <<~RUBY
  # From the MIDoRI reference. dev / fx play the built-in synth (the SAM2695).
  dev = MIDI::Device.new(MIDIDevices.sam2695)
  fx = dev
RUBY
LOOP = <<~RUBY

  loop do
    UI.process
    MIDI.sleep_ms 10
  end
RUBY
SKIP = [/\.\.\./, /…/, /BLE_MIDI|ble_midi/]

dump = ARGV[0] == "--dump" ? File.expand_path(ARGV[1], Dir.pwd) : nil
FileUtils.mkdir_p(dump) if dump

def share_code(text)
  z = Zlib::Deflate.new(Zlib::BEST_COMPRESSION, -Zlib::MAX_WBITS)
  bytes = z.deflate(text, Zlib::FINISH)
  z.close
  Base64.urlsafe_encode64(bytes, padding: false)
end

PAGES.each do |rel, opt|
  path = File.join(ROOT, rel)
  html = File.read(path)
  # A link from an earlier run goes first
  html = html.gsub(%r{\n?\s*<a class="sim-link"[^>]*>[^<]*</a>(?=</div>)}, "")
  linked = 0
  n = 0
  html = html.gsub(%r{(<div class="code-card"><pre>)(.*?)(</pre>)(</div>)}m) do
    head, body, close, tail = $1, $2, $3, $4
    n += 1
    snippet = CGI.unescapeHTML(body)
    next "#{head}#{body}#{close}#{tail}" if SKIP.any? { |re| snippet.match?(re) }
    script = PRELUDE + snippet.rstrip + "\n" + (snippet.include?("loop do") ? "" : LOOP)
    if dump
      File.write(File.join(dump, "#{File.basename(File.dirname(rel))}-#{opt[:lang]}-#{format('%02d', n)}.rb"), script)
    end
    query = "?lang=#{opt[:lang]}"
    href = "#{opt[:href]}#{query}#code=#{share_code(script)}&board=tab5"
    linked += 1
    "#{head}#{body}#{close}<a class=\"sim-link\" href=\"#{CGI.escapeHTML(href)}\" target=\"_blank\" rel=\"noopener\">#{opt[:label]}</a>#{tail}"
  end
  File.write(path, html)
  puts "#{rel}: #{linked} of #{n} code cards linked"
end
