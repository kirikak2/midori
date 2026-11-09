#!/usr/bin/env python3
"""Write ESP Web Tools manifests for the merged images in a directory.

Usage: make_manifests.py <firmware-dir> <version>

For every midori-<version>-<board>-<mode>.bin it writes
<board>-<mode>.json (one manifest per image: the CoreS3 and the Freenove are
both ESP32-S3, so the chip family alone cannot pick the image), and an
index.json the install page reads to fill in its board and mode choices.
"""

import json
import pathlib
import re
import sys

BOARDS = {
    "m5stack":      {"chip": "ESP32-S3", "name": "M5Stack CoreS3 / CoreS3 SE"},
    "m5stack_tab5": {"chip": "ESP32-P4", "name": "M5Stack Tab5"},
    "crowpanel":    {"chip": "ESP32-P4", "name": "Elecrow CrowPanel Advanced 7\""},
    "freenove":     {"chip": "ESP32-S3", "name": "Freenove ESP32-S3 (no screen)"},
}
MODES = ["host", "midi_device"]


def main() -> None:
    fw_dir = pathlib.Path(sys.argv[1])
    version = sys.argv[2]
    pattern = re.compile(
        r"^midori-" + re.escape(version) + r"-(?P<board>[a-z0-9_]+?)-(?P<mode>host|midi_device|serial)\.bin$"
    )

    images = []
    for path in sorted(fw_dir.glob("*.bin")):
        m = pattern.match(path.name)
        if not m or m["board"] not in BOARDS:
            print(f"skip {path.name}", file=sys.stderr)
            continue
        board, mode = m["board"], m["mode"]
        manifest = {
            "name": f"MIDoRI ({BOARDS[board]['name']}, {mode})",
            "version": version,
            # The root volume sits at a fixed flash address and is formatted
            # on the first boot; erasing keeps an old NVS (saved patches,
            # settings from another build) from carrying over.
            "new_install_prompt_erase": True,
            # MIDoRI does not speak Improv (Wi-Fi provisioning over serial):
            # don't wait for it after flashing.
            "new_install_improv_wait_time": 0,
            "builds": [
                {
                    "chipFamily": BOARDS[board]["chip"],
                    "parts": [{"path": path.name, "offset": 0}],
                }
            ],
        }
        out = fw_dir / f"{board}-{mode}.json"
        out.write_text(json.dumps(manifest, indent=2) + "\n")
        images.append({
            "board": board,
            "mode": mode,
            "chip": BOARDS[board]["chip"],
            "manifest": out.name,
            "file": path.name,
            "size": path.stat().st_size,
        })

    images.sort(key=lambda i: (list(BOARDS).index(i["board"]), MODES.index(i["mode"]) if i["mode"] in MODES else 9))
    (fw_dir / "index.json").write_text(
        json.dumps({"version": version, "images": images}, indent=2) + "\n"
    )
    print(f"{len(images)} manifests for {version}")


if __name__ == "__main__":
    main()
