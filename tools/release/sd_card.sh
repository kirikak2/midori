#!/bin/bash
# Package the microSD card contents shipped with a release.
#
# Usage: tools/release/sd_card.sh [out-dir]
#
# Run after a firmware build (it uses the host mrbc that build made).
# Produces midori-<ver>-sd-card.zip; its contents go to the root of a
# FAT32 card:
#
#   /*.rb                  example scripts (the Scripts screen lists these)
#   /plugins/bundle.mrb    the example plugins (filters, layouts) in one file
#   /README.txt

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$(mkdir -p "${1:-$ROOT/dist}" && cd "${1:-$ROOT/dist}" && pwd)"
VERSION="${MIDORI_VERSION:-$(git -C "$ROOT" describe --tags --always --dirty 2>/dev/null || echo dev)}"
NAME="midori-${VERSION}-sd-card"

STAGE="$(mktemp -d)"
mkdir -p "$STAGE/plugins"
cp "$ROOT"/examples/*.rb "$STAGE/"
# Only the bundle: the plugin sources next to it would be loaded twice.
ruby "$ROOT/tools/bundle_plugins.rb" "$ROOT/examples/plugins" "$STAGE/plugins/bundle.mrb"
cat > "$STAGE/README.txt" <<EOF
MIDoRI ${VERSION} - microSD card contents

Copy everything in this folder to the root of a FAT32-formatted microSD card.

  *.rb                  Example scripts. Pick one on the Scripts screen.
  plugins/bundle.mrb    Example plugins (MIDI filters: chord, arpeggio,
                        channel map; layouts: Perform, Keys, Clock),
                        loaded at every start.

Some examples need particular hardware: amy_* and keys_amy need a board
with a speaker (Tab5 / CrowPanel), dfrobot_* need DFRobot rotary encoders.
EOF
(cd "$STAGE" && rm -f "$OUT/$NAME.zip" && python3 -m zipfile -c "$OUT/$NAME.zip" *)
rm -rf "$STAGE"
echo "Built $OUT/$NAME.zip"
