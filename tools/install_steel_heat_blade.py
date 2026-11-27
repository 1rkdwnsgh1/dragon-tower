"""Install byte-exact V6.1 game atlases and generate their checked C++ layout."""
import argparse
from hashlib import sha256
from io import BytesIO
import json
from pathlib import Path, PurePosixPath
from zipfile import ZipFile

from PIL import Image

def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("archive", type=Path)
    cli.add_argument("--root", type=Path, default=Path.cwd())
    cli.add_argument("--report", type=Path, required=True)
    args = cli.parse_args()
    assets = args.root / "assets/dragons/steel_dragon/heat_blade"
    assets.mkdir(parents=True, exist_ok=True)
    args.report.mkdir(parents=True, exist_ok=True)
    digest = sha256(args.archive.read_bytes()).hexdigest()
    assert digest == "12808a2e5d9213d370dc48c4c659b5d9e2faadf810266ba39d7d4546fd25f44e"
    inventory = {}
    with ZipFile(args.archive) as archive:
        assert archive.testzip() is None
        for name in archive.namelist():
            if name.lower().endswith(".png"):
                data = archive.read(name)
                with Image.open(BytesIO(data)) as image:
                    image.load()
                    inventory[name] = {"size": image.size, "mode": image.mode, "sha256": sha256(data).hexdigest()}
        spec = json.loads(archive.read("metadata.json"))
        config = spec["skill"]
        assert (config["duration_s"], config["fps"], config["orbit"]["blades"]) == (4.0, 60, 4)
        assert [(e["tick"], e["count"]) for e in config["projectile"]["events"]] == [(36,3),(108,3),(180,3)]
        assert len(spec["layers"]) == 33
        rows = []
        def atlas(name, w, h, frames, fps, anchor, scale, z=0, delay=0, stop=0, loop=False, mirror=False):
            record = inventory[name]
            width, height = record["size"]
            assert record["mode"] == "RGBA" and width % w == 0 and height % h == 0
            columns = width // w
            assert (frames + columns - 1) // columns == height // h
            data = archive.read(name)
            local = str(PurePosixPath(name).relative_to("assets"))
            path = assets / local
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            assert path.read_bytes() == data
            record.update(cell=[w,h], frames=frames, columns=columns, anchor=anchor)
            return f'{{"{local}", {w}, {h}, {frames}, {fps}, {anchor[0]}, {anchor[1]}, {scale:.5f}f, {z}, {delay}, {stop}, {str(loop).lower()}, {str(mirror).lower()}}}'
        for layer in sorted(spec["layers"], key=lambda d: d["z"]):
            assert layer["attach"] == "body"
            rows.append(atlas(layer["file"], *layer["cell"], layer["frame_count"], layer["fps"],
                              layer["anchor"], layer["scale"], layer["z"], round(layer["delay"]*60),
                              round(layer["stop"]*60), layer["loop"], layer["mirror"]))
        projectile = config["projectile"]
        flight = projectile["flight_sprites"]
        for direction in ("left","right"):
            rows.append(atlas(flight[direction], *flight["cell_px"], flight["frames"], flight["fps"],
                              flight[direction+"_anchor_px"], .4, loop=True))
        for part in ("launch","contact","die"):
            entry = projectile[part]
            rows.append(atlas(entry["file"], *entry["cell_px"], entry["frames"], entry["fps"], entry["anchor_px"], .4))
        for ident, body in spec["bodies"].items():
            data = archive.read(body["file"])
            assert (assets.parent/PurePosixPath(body["file"]).name).read_bytes() == data, ident
        (assets/"metadata.json").write_bytes(archive.read("metadata.json"))
        for name in ("preview/game_scale.png","preview/full_skill_4s.apng","qa/validation_summary.md"):
            (args.report/PurePosixPath(name).name).write_bytes(archive.read(name))
    (args.report/"asset-qa.json").write_text(json.dumps({"zip_sha256":digest,"png":inventory},indent=2),encoding="utf8")
    header = """// Generated from the approved V6.1 metadata by tools/install_steel_heat_blade.py.
#pragma once

namespace dt::win::heatBlade {
struct Atlas {
    const char* path;
    int width, height, frames, fps, anchorX, anchorY;
    float scale;
    int z, startTick, stopTick;
    bool loop, mirror;
};
inline constexpr Atlas atlases[] = {
"""
    header += "".join("    "+row+",\n" for row in rows) + """};
inline constexpr int orbitLayers = 33;
inline constexpr int flightLeft = 33, flightRight = 34, launch = 35, contact = 36, disappear = 37;
}  // namespace dt::win::heatBlade
"""
    (args.root/"src/win/HeatBladeAtlases.h").write_text(header,encoding="utf8",newline="\n")

if __name__ == "__main__":
    main()
