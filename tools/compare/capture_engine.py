#!/usr/bin/env python3
"""Capture the plain engine simulator in the same format as the other two.

This is the third side of the comparison. The snapshot harness runs the whole
ESPHome display front end; this runs nothing but ``Engine::render()`` through
``wled_fx_sim --anim``, with no ESPHome in the picture at all. When the port
disagrees with the device and this agrees with it, the fault is in the front
end; when both disagree the same way, it is in the engine.

    tools/compare/capture_engine.py --out DIR --match-reference REFDIR

``--match-reference`` is not optional in practice. Round 1 captured this side
at each effect's own metadata defaults while the other two sides were at
whatever the device happened to have, and on a 50 ms clock against their 23 ms,
and then used the difference to decide which half of the stack a fault was in.
That answer was worthless. With ``--match-reference`` every control the
reference recorded is pinned here too, the frame period is the device's, and
the per-effect PACING table is off, so the three captures are the same
animation at the same moment.

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
# at with no --step-ms.
ANIM_STEP_MS = 50
# WLED's nominal frame period, which is what the device and the snapshot
# harness both run at, and what a comparison run has to use.
COMPARISON_STEP_MS = 23
CANVAS = (64, 64)

# The reference's applied_state_fields keys, and the simulator option each one
# pins. `fx` and `colours` are handled separately.
FIELD_OPTIONS = {
    "pal": "--palette",
    "sx": "--speed",
    "ix": "--intensity",
    "c1": "--custom1",
    "c2": "--custom2",
    "c3": "--custom3",
    "o1": "--check1",
    "o2": "--check2",
    "o3": "--check3",
    "m12": "--map",
}

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


def load_reference_settings(root: Path) -> dict[str, dict]:
    """What the device was actually set to, per effect, from its own captures."""
    out: dict[str, dict] = {}
    cap_dir = root / "captures"
    if not cap_dir.is_dir():
        sys.exit(f"no captures/ under {root}")
    for d in sorted(cap_dir.iterdir()):
        meta_path = d / "meta.json"
        if not meta_path.exists():
            continue
        try:
            meta = json.loads(meta_path.read_text())
        except Exception:  # noqa: BLE001
            continue
        fields = meta.get("applied_state_fields")
        if meta.get("name") and isinstance(fields, dict):
            out[meta["name"]] = fields
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    ap.add_argument("--effects", default=None, help="comma separated names, default all")
    ap.add_argument("--frames", type=int, default=0,
                    help="frames per effect, default 120 free running or 260 with --match-reference")
    ap.add_argument("--match-reference", default=None,
                    help="a reference capture folder: pin every control it recorded, "
                         "run at the device's frame period and turn the PACING table off")
    args = ap.parse_args()

    matched = load_reference_settings(Path(args.match_reference)) if args.match_reference else {}
    if args.match_reference:
        print(f"matching {len(matched)} effect(s) to {args.match_reference}", flush=True)
    step_ms = COMPARISON_STEP_MS if args.match_reference else ANIM_STEP_MS
    frames_wanted = args.frames or (260 if args.match_reference else 120)

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
            fields = matched.get(name, {})
            command = [
                str(sim), "--effect", name, "--size", f"{CANVAS[0]}x{CANVAS[1]}",
                "--frames", str(frames_wanted), "--anim", str(raw_dir),
                "--color1", COLORS[0], "--color2", COLORS[1], "--color3", COLORS[2],
            ]
            if args.match_reference:
                command += ["--step-ms", str(step_ms)]
                for key, option in FIELD_OPTIONS.items():
                    if key not in fields:
                        continue
                    value = fields[key]
                    command += [option, str(int(bool(value)) if isinstance(value, bool) else int(value))]
            proc = subprocess.run(command, capture_output=True, text=True)
            manifest = [line for line in proc.stdout.splitlines() if line.startswith("anim\t")]
            if not manifest:
                print(f"  no frames for {name}: {proc.stderr.strip()[:160]}", flush=True)
                continue
            _, path, w, h, count, _ = manifest[0].split("\t", 5)
            w, h, count = int(w), int(h), int(count)
            data = np.frombuffer(Path(path).read_bytes(), dtype=np.uint8)
            frames = data[: count * h * w * 3].reshape((count, h, w, 3)).copy()
            ts = np.arange(count, dtype=np.float64) * (step_ms / 1000.0)

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
                        "measured_fps": round(1000 / step_ms, 2),
                        "received_size": {"width": w, "height": h},
                        "applied": {
                            "selected_by": (
                                "name, with every control the reference recorded pinned on top"
                                if args.match_reference
                                else "name, which applies the effect's WLED metadata defaults"
                            ),
                            "matched_to_reference": args.match_reference,
                            "fields": fields or None,
                            "colors": [[255, 160, 0], [0, 0, 0], [0, 0, 0]],
                            "gamma_correct": 1.0,
                            "pacing_table": "off" if args.match_reference else "on",
                            "clock": f"virtual, {step_ms} ms a frame, so these timestamps are exact",
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
