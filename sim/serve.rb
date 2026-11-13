#!/usr/bin/env ruby
# Serves sim/web for development without browser caching.
#
# A plain static server sends no cache headers, so a browser may keep files
# from an earlier build (the page's scripts, the boot Ruby) and mix them with
# new ones. This one says Cache-Control: no-store.
#
#   ruby sim/serve.rb [port]      # default 8080, http://localhost:8080/
#
# WEBrick is a gem since Ruby 3.0: gem install webrick
begin
  require "webrick"
rescue LoadError
  abort "WEBrick is not installed: gem install webrick"
end

port = (ARGV[0] || 8080).to_i
root = File.expand_path("web", __dir__)

mime = WEBrick::HTTPUtils::DefaultMimeTypes.merge(
  "wasm" => "application/wasm",
  "js"   => "text/javascript",
  "mjs"  => "text/javascript",
  "rb"   => "text/plain; charset=utf-8",
  "json" => "application/json"
)

server = WEBrick::HTTPServer.new(
  Port: port,
  DocumentRoot: root,
  MimeTypes: mime,
  AccessLog: [],
  Logger: WEBrick::Log.new($stderr, WEBrick::Log::WARN)
)

# No caching of anything served
class << server
  def service(req, res)
    super
    res["Cache-Control"] = "no-store"
  end
end

trap("INT") { server.shutdown }
trap("TERM") { server.shutdown }
puts "Serving #{root} on http://localhost:#{port}/ (no-store)"
server.start
