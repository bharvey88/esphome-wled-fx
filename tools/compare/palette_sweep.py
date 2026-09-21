#!/usr/bin/env python3
"""Sweep every palette on a handful of effects, on the device and in the port.

The round 1 and round 2 comparisons captured each effect once, on the palette
that effect's own metadata declares. That leaves 71 of WLED's 72 palettes
unmeasured on every effect, which is how a palette-only defect survived two
adversarial rounds.

Three subcommands, one per side plus the report:

    palette_sweep.py device --out DIR [--host H] [--effects "A,B"] [--seconds S]
    palette_sweep.py port   --out DIR [--effects "A,B"]
    palette_sweep.py report --device DIR --port DIR --out FILE.md

``device`` talks to a real WLED device over the JSON API and the live view
websocket only, and restores the state it found. ``port`` runs the engine
simulator, which produces the same pre-output-gamma buffer the live view sends,
so the two are directly comparable without a gamma stage on either side.
``--with-gamma`` adds a second port capture through the output gamma table, for
the cases where the question is what reaches the panel rather than what the
engine drew.

Per effect and palette both sides record mean brightness, the three channel
means, the fraction of lit pixels and a twelve bin hue histogram. The report
puts them side by side and scores a palette against ``NOISE.md``: a difference
counts when it is outside the class floor the device's disagreement with itself
sets.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

import numpy as np

REPO = Path(__file__).resolve().parents[2]
# Only this directory. `components/wled_fx` is deliberately not on the path:
# it holds a package called `select`, and putting it first shadows the standard
# library module of that name, which breaks `requests` two imports later.
sys.path.insert(0, str(Path(__file__).resolve().parent))

from metrics import compute_metrics, hue_distance, subsample  # noqa: E402

# The engine's canvas for a sweep. The device is a 64x64 panel whose live view
# arrives nearest-subsampled to 32x32, so the port is rendered at 64x64 and put
# through the same subsampling rather than rendered small.
CANVAS = (64, 64)
LIVEVIEW = (32, 32)
# WLED's nominal frame period, which is what the device runs at.
STEP_MS = 23
DEFAULT_FRAMES = 200
DEFAULT_SECONDS = 4.0

# WLED's DEFAULT_COLOR and two empty slots, which is what the reference runs
# used. Four of the seventy-two palettes are built from these, so they have to
# be the same on both sides or those four cannot be compared at all.
COLORS_RGB = ((255, 160, 0), (0, 0, 0), (0, 0, 0))
COLORS_HEX = ("FFA000", "000000", "000000")

# One effect from each family that reads a palette a different way. Small on
# purpose: the device needs about ten seconds per palette per effect.
DEFAULT_EFFECTS = (
    "Palette",       # color_from_palette once per pixel, the plainest reader
    "Fire 2012",     # ColorFromPalette with a computed brightness
    "Noise 1",       # palette index from a noise field
    "Hiphotic",      # 2D, palette index from two sine fields
    "PS Fireworks",  # a particle system, which reads the palette per particle
)

PALETTE_COUNT = 72


def log(msg: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)


def slug(name: str) -> str:
    return re.sub(r"[^A-Za-z0-9]+", "_", name).strip("_") or "unnamed"


def palette_names() -> list[str]:
    """The port's own palette list, read out of the source it is compiled from."""
    src = (REPO / "components" / "wled_fx" / "wf_palette_util.cpp").read_text(encoding="utf-8")
    block = re.search(r"PALETTE_NAMES\[\]\s*=\s*\{(.*?)\};", src, re.S)
    if not block:
        raise SystemExit("could not find PALETTE_NAMES in wf_palette_util.cpp")
    return re.findall(r'"((?:[^"\\]|\\.)*)"', block.group(1))


def summarise(frames: np.ndarray, timestamps: np.ndarray) -> dict:
    m = compute_metrics(frames, timestamps)
    f = frames.astype(np.float64)
    return {
        "mean_brightness": round(float(m["mean_brightness"]), 3),
        "fraction_lit": round(float(m["fraction_lit"]), 4),
        "mean_frame_change": round(float(m["mean_frame_change"]), 3),
        "channel_means": [round(float(f[..., i].mean()), 3) for i in range(3)],
        "hue_histogram": m["dominant_hue_histogram_12bin"],
        "hue_samples_per_frame": m["hue_samples_per_frame"],
        "frame_count": int(frames.shape[0]),
    }


# ---------------------------------------------------------------------------
# The port side
# ---------------------------------------------------------------------------


def find_sim() -> Path:
    import os

    if override := os.environ.get("WLED_FX_SIM"):
        return Path(override)
    for candidate in (
        Path("/root/wfx/sim-asan/wled_fx_sim"),
        Path("/root/wfx/sim-plain/wled_fx_sim"),
        REPO / "tools" / "sim" / "build" / "wled_fx_sim",
    ):
        if candidate.exists():
            return candidate
    sys.exit("no wled_fx_sim found; build it or set WLED_FX_SIM")


def output_gamma_lut(gamma: float) -> np.ndarray:
    """build_output_gamma_lut() from wf_color.cpp, in numpy.

    The engine simulator hands back the pre-output buffer, which is what the
    device's live view sends. This is the stage the display front end adds on
    top, so a sweep can be read either side of it.
    """
    x = np.arange(256, dtype=np.float64) / 255.0
    return np.clip(np.round((x ** gamma) * 255.0), 0, 255).astype(np.uint8)


def capture_port(out: Path, effects: list[str], frames_wanted: int, gamma: float | None) -> int:
    sim = find_sim()
    names = palette_names()
    out.mkdir(parents=True, exist_ok=True)
    lut = output_gamma_lut(gamma) if gamma else None
    records = []
    with tempfile.TemporaryDirectory() as tmp:
        for effect in effects:
            for pal in range(PALETTE_COUNT):
                raw = Path(tmp) / "raw"
                raw.mkdir(exist_ok=True)
                command = [
                    str(sim), "--effect", effect,
                    "--size", f"{CANVAS[0]}x{CANVAS[1]}",
                    "--frames", str(frames_wanted), "--step-ms", str(STEP_MS),
                    "--palette", str(pal),
                    "--color1", COLORS_HEX[0], "--color2", COLORS_HEX[1], "--color3", COLORS_HEX[2],
                    "--anim", str(raw),
                ]
                proc = subprocess.run(command, capture_output=True, text=True)
                manifest = [line for line in proc.stdout.splitlines() if line.startswith("anim\t")]
                if not manifest:
                    log(f"  no frames for {effect} pal {pal}: {proc.stderr.strip()[:160]}")
                    continue
                _, path, w, h, count, _ = manifest[0].split("\t", 5)
                w, h, count = int(w), int(h), int(count)
                data = np.frombuffer(Path(path).read_bytes(), dtype=np.uint8)
                full = data[: count * h * w * 3].reshape((count, h, w, 3)).copy()
                Path(path).unlink(missing_ok=True)
                small = subsample(full, LIVEVIEW[0], LIVEVIEW[1])
                ts = np.arange(small.shape[0], dtype=np.float64) * (STEP_MS / 1000.0)
                record = {
                    "effect": effect,
                    "palette": pal,
                    "palette_name": names[pal],
                    "pre_gamma": summarise(small, ts),
                }
                if lut is not None:
                    record["post_gamma"] = summarise(lut[small], ts)
                records.append(record)
            log(f"  {effect}: {PALETTE_COUNT} palettes")
    write_side(out, "port", records, {
        "source": "wled_fx_sim --anim, the engine with no ESPHome around it",
        "canvas": f"{CANVAS[0]}x{CANVAS[1]} rendered, subsampled to {LIVEVIEW[0]}x{LIVEVIEW[1]}",
        "step_ms": STEP_MS,
        "frames": frames_wanted,
        "colors": [list(c) for c in COLORS_RGB],
        "output_gamma": gamma,
    })
    return 0


# ---------------------------------------------------------------------------
# The device side
# ---------------------------------------------------------------------------


def capture_device(out: Path, host: str, effects: list[str], seconds: float) -> int:
    sys.path.insert(0, str(REPO / "tools" / "reference"))
    import capture_wled as cw  # noqa: PLC0415

    names = palette_names()
    out.mkdir(parents=True, exist_ok=True)
    original = cw.save_original_state(host, out)
    records = []
    try:
        catalog = cw.fetch_effect_catalog(host, out)
        by_name = {e["name"].strip().lower(): e for e in catalog}
        missing = [e for e in effects if e.strip().lower() not in by_name]
        if missing:
            log(f"WARNING: not in the device catalog: {missing}")
        cw.apply_colors(host, COLORS_RGB)
        time.sleep(0.5)
        for effect in effects:
            entry = by_name.get(effect.strip().lower())
            if entry is None:
                continue
            for pal in range(PALETTE_COUNT):
                frames_raw, timestamps, applied, error = cw.capture_one_effect(
                    host, entry, seconds, colors=COLORS_RGB, extra_fields={"pal": pal}
                )
                if not frames_raw:
                    log(f"  {effect} pal {pal}: no frames ({error})")
                    continue
                from collections import Counter  # noqa: PLC0415

                sizes = Counter((w, h) for w, h, _ in frames_raw)
                (mw, mh), _ = sizes.most_common(1)[0]
                kept = [(a, t) for (w, h, a), t in zip(frames_raw, timestamps) if (w, h) == (mw, mh)]
                frames = np.stack([a for a, _ in kept]).astype(np.uint8)
                ts = np.array([t for _, t in kept], dtype=np.float64)
                seg0 = (applied or {}).get("seg", [{}])[0] if applied else {}
                records.append({
                    "effect": effect,
                    "palette": pal,
                    "palette_name": names[pal],
                    "palette_readback": seg0.get("pal"),
                    "received_size": [int(mw), int(mh)],
                    "pre_gamma": summarise(frames, ts),
                })
                time.sleep(cw.INTER_EFFECT_PAUSE)
            log(f"  {effect}: {PALETTE_COUNT} palettes")
    finally:
        cw.restore_device_state(host, out, original)
    write_side(out, "device", records, {
        "source": "WLED live view websocket, the pre-output buffer before gamma and brightness",
        "host": host,
        "seconds": seconds,
        "colors": [list(c) for c in COLORS_RGB],
        "output_gamma": None,
    })
    return 0


def write_side(out: Path, side: str, records: list[dict], facts: dict) -> None:
    (out / "sweep.json").write_text(json.dumps({"side": side, "facts": facts, "records": records}, indent=2))
    log(f"wrote {len(records)} record(s) to {out / 'sweep.json'}")


# ---------------------------------------------------------------------------
# The report
# ---------------------------------------------------------------------------

# NOISE.md, the p95 of the device against itself. A palette sweep runs one
# effect at a time on non-audio effects, so "other" and "particle" are the two
# that apply.
FLOORS = {
    "other": {"mean_brightness": 24.0, "ratio_lo": 0.75, "ratio_hi": 1.33,
              "fraction_lit": 0.05, "hue_distance": 0.78},
    "particle": {"mean_brightness": 8.0, "ratio_lo": 0.80, "ratio_hi": 1.25,
                 "fraction_lit": 0.06, "hue_distance": 0.82},
}


def class_of(effect: str) -> str:
    return "particle" if effect.startswith("PS ") else "other"


def load_side(path: Path) -> tuple[dict[tuple[str, int], dict], dict]:
    blob = json.loads((path / "sweep.json").read_text())
    return {(r["effect"], r["palette"]): r for r in blob["records"]}, blob.get("facts", {})


def compare_rows(device: dict, port: dict) -> list[dict]:
    rows = []
    for key in sorted(set(device) & set(port)):
        effect, pal = key
        d = device[key]["pre_gamma"]
        p = port[key]["pre_gamma"]
        floors = FLOORS[class_of(effect)]
        db, pb = d["mean_brightness"], p["mean_brightness"]
        ratio = (pb / db) if db > 0.5 else None
        hue = hue_distance(d.get("hue_histogram"), p.get("hue_histogram"))
        flags = []
        if abs(pb - db) > floors["mean_brightness"] and (
            ratio is None or ratio < floors["ratio_lo"] or ratio > floors["ratio_hi"]
        ):
            flags.append("brightness")
        if abs(p["fraction_lit"] - d["fraction_lit"]) > floors["fraction_lit"]:
            flags.append("coverage")
        if hue > floors["hue_distance"]:
            flags.append("hue")
        rows.append({
            "effect": effect,
            "palette": pal,
            "palette_name": device[key]["palette_name"],
            "device_brightness": db,
            "port_brightness": pb,
            "ratio": round(ratio, 3) if ratio is not None else None,
            "device_channels": d["channel_means"],
            "port_channels": p["channel_means"],
            "device_lit": d["fraction_lit"],
            "port_lit": p["fraction_lit"],
            "hue_distance": round(hue, 3),
            "flags": flags,
        })
    return rows


def report(device_dir: Path, port_dir: Path, out: Path) -> int:
    device, dfacts = load_side(device_dir)
    port, pfacts = load_side(port_dir)
    rows = compare_rows(device, port)
    if not rows:
        sys.exit("no (effect, palette) pair is present on both sides")
    flagged = [r for r in rows if r["flags"]]
    lines = [
        "# Palette sweep",
        "",
        f"{len(rows)} effect and palette pairs, {len(flagged)} flagged against the "
        "`NOISE.md` floors.",
        "",
        f"* device: `{device_dir}`, {dfacts.get('source', '')}",
        f"* port: `{port_dir}`, {pfacts.get('source', '')}",
        "",
        "## Per effect",
        "",
        "| Effect | palettes | worst brightness ratio | median ratio | worst hue | flagged |",
        "|---|---:|---|---:|---|---:|",
    ]
    for effect in sorted({r["effect"] for r in rows}):
        sub = [r for r in rows if r["effect"] == effect]
        ratios = [(r["ratio"], r["palette_name"]) for r in sub if r["ratio"] is not None]
        # The ratio furthest from 1.0 in either direction.
        worst = max(ratios, key=lambda t: abs(np.log(max(t[0], 1e-6)))) if ratios else (float("nan"), "-")
        med = float(np.median([t[0] for t in ratios])) if ratios else float("nan")
        hues = [(r["hue_distance"], r["palette_name"]) for r in sub]
        wh = max(hues, key=lambda t: t[0])
        lines.append(
            f"| {effect} | {len(sub)} | "
            f"{worst[0]:.2f} ({worst[1]}) | {med:.2f} | {wh[0]:.2f} ({wh[1]}) | "
            f"{sum(1 for r in sub if r['flags'])} |"
        )
    lines += ["", "## Flagged pairs", ""]
    if not flagged:
        lines.append("None.")
    else:
        lines += [
            "| Effect | Palette | device bri | port bri | ratio | hue | flags |",
            "|---|---|---:|---:|---:|---:|---|",
        ]
        for r in sorted(flagged, key=lambda r: (r["effect"], r["palette"])):
            lines.append(
                f"| {r['effect']} | {r['palette']} {r['palette_name']} | "
                f"{r['device_brightness']:.1f} | {r['port_brightness']:.1f} | "
                f"{'' if r['ratio'] is None else format(r['ratio'], '.2f')} | "
                f"{r['hue_distance']:.2f} | {', '.join(r['flags'])} |"
            )
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    out.with_suffix(".json").write_text(json.dumps(rows, indent=2))
    log(f"wrote {out} and {out.with_suffix('.json')}")
    print(f"{len(flagged)} of {len(rows)} pairs flagged")
    return 1 if flagged else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="mode", required=True)

    d = sub.add_parser("device", help="capture every palette from a real WLED device")
    d.add_argument("--out", required=True)
    d.add_argument("--host", default="10.10.10.225")
    d.add_argument("--effects", default=",".join(DEFAULT_EFFECTS))
    d.add_argument("--seconds", type=float, default=DEFAULT_SECONDS)

    p = sub.add_parser("port", help="capture every palette from the engine simulator")
    p.add_argument("--out", required=True)
    p.add_argument("--effects", default=",".join(DEFAULT_EFFECTS))
    p.add_argument("--frames", type=int, default=DEFAULT_FRAMES)
    p.add_argument("--with-gamma", type=float, default=None,
                   help="also record the frames through the output gamma table at this exponent")

    r = sub.add_parser("report", help="put the two sides side by side")
    r.add_argument("--device", required=True)
    r.add_argument("--port", required=True)
    r.add_argument("--out", required=True)

    args = ap.parse_args()
    if args.mode == "port":
        effects = [e.strip() for e in args.effects.split(",") if e.strip()]
        return capture_port(Path(args.out), effects, args.frames, args.with_gamma)
    if args.mode == "device":
        effects = [e.strip() for e in args.effects.split(",") if e.strip()]
        return capture_device(Path(args.out), args.host, effects, args.seconds)
    return report(Path(args.device), Path(args.port), Path(args.out))


if __name__ == "__main__":
    sys.exit(main())
