#!/usr/bin/env python3
"""Capture the plain engine simulator in the same format as the other two.

This is the third side of the comparison. The snapshot harness runs the whole
ESPHome display front end; this runs nothing but ``Engine::render()`` through
``wled_fx_sim --anim``, with no ESPHome in the picture at all. When the port
disagrees with the device and this agrees with it, the fault is in the front
end; when both disagree the same way, it is in the engine.

    tools/compare/capture_engine.py --out DIR

The simulator's clock is virtual: it steps in fixed increments and the frames
are reproducible. Timestamps here are therefore exact, which makes the speed in
pixels per second exact too, and that is the number the comparison leans on.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

import numpy as np

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "components" / "wled_fx"))
sys.path.insert(0, str(REPO / "tools" / "snapshot"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from effect_index import effect_names  # noqa: E402
from metrics import compute_metrics  # noqa: E402

# Matches ANIM_STEP_MS in tools/sim/main.cpp, which is the rate --anim renders
# at and the rate these timestamps have to be built from.
ANIM_STEP_MS = 50
CANVAS = (64, 64)

# The colours the device was captured with, which is WLED's DEFAULT_COLOR and
# two empty slots. The simulator would otherwise use the engine's own defaults,
# and an effect that paints the primary colour would report a different hue for
# a reason that has nothing to do with the effect.
COLORS = ("FFA000", "000000", "000000")


def find_sim() -> Path:
    import os

    if override := os.environ.get("WLED_FX_SIM"):
        return Path(override)
    for candidate in (
        Path("/root/wfx/sim-asan/wled_fx_sim"),
        Path("/root/wfx/sim-plain/wled_fx_sim"),
        REPO / "tools" / "sim" / "build" / "wled_fx_sim",
        REPO / "tools" / "sim" / "build" / "wled_fx_sim.exe",
    ):
        if candidate.exists():
            return candidate
    sys.exit("no wled_fx_sim found; build it or set WLED_FX_SIM")


def safe_folder_name(name: str, index: int) -> str:
    slug = re.sub(r"[^A-Za-z0-9]+", "_", name).strip("_") or "unnamed"
    return f"{slug}_{index:03d}"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    ap.add_argument("--effects", default=None, help="comma separated names, default all")
    ap.add_argument("--frames", type=int, default=120, help="frames per effect, 120 is six seconds")
    args = ap.parse_args()

    sim = find_sim()
    names = effect_names()
    if args.effects:
        wanted = {n.strip().casefold() for n in args.effects.split(",") if n.strip()}
        names = [n for n in names if n.casefold() in wanted]

    from capture_port import CAPTURE_NOTES, make_anim_webp, make_contact_sheet  # noqa: F401

    out = Path(args.out)
    (out / "captures").mkdir(parents=True, exist_ok=True)
    index_of = {n: i for i, n in enumerate(effect_names())}

    packed = 0
    with tempfile.TemporaryDirectory() as tmp:
        for name in names:
            raw_dir = Path(tmp) / "raw"
            raw_dir.mkdir(exist_ok=True)
            command = [
                str(sim), "--effect", name, "--size", f"{CANVAS[0]}x{CANVAS[1]}",
                "--frames", str(args.frames), "--anim", str(raw_dir),
                "--color1", COLORS[0], "--color2", COLORS[1], "--color3", COLORS[2],
            ]
            proc = subprocess.run(command, capture_output=True, text=True)
            manifest = [line for line in proc.stdout.splitlines() if line.startswith("anim\t")]
            if not manifest:
                print(f"  no frames for {name}: {proc.stderr.strip()[:160]}", flush=True)
                continue
            _, path, w, h, count, _ = manifest[0].split("\t", 5)
            w, h, count = int(w), int(h), int(count)
            data = np.frombuffer(Path(path).read_bytes(), dtype=np.uint8)
            frames = data[: count * h * w * 3].reshape((count, h, w, 3)).copy()
            ts = np.arange(count, dtype=np.float64) * (ANIM_STEP_MS / 1000.0)

            folder = safe_folder_name(name, index_of.get(name, 999))
            effect_dir = out / "captures" / folder
            effect_dir.mkdir(parents=True, exist_ok=True)
            np.savez_compressed(effect_dir / "frames.npz", frames=frames, timestamps=ts)
            make_contact_sheet(frames, ts.tolist(), effect_dir / "sheet.png")
            make_anim_webp(frames, ts.tolist(), effect_dir / "anim.webp")
            (effect_dir / "meta.json").write_text(
                json.dumps(
                    {
                        "id": index_of.get(name, 999),
                        "name": name,
                        "folder": folder,
                        "status": "ok",
                        "source": "wled_fx_sim --anim, the engine with no ESPHome around it",
                        "canvas": {"width": w, "height": h},
                        "frame_count": int(count),
                        "measured_fps": round(1000 / ANIM_STEP_MS, 2),
                        "received_size": {"width": w, "height": h},
                        "applied": {
                            "selected_by": "name, which applies the effect's WLED metadata defaults",
                            "colors": [[255, 160, 0], [0, 0, 0], [0, 0, 0]],
                            "clock": "virtual, 50 ms a frame, so these timestamps are exact",
                        },
                        "metrics": compute_metrics(frames, ts),
                    },
                    indent=2,
                )
            )
            Path(path).unlink(missing_ok=True)
            packed += 1
            if packed % 25 == 0:
                print(f"  {packed}/{len(names)}", flush=True)

    print(f"packed {packed} effect(s) into {out}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
