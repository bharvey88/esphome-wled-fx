#!/usr/bin/env python3
"""Build the animated effect gallery under docs/gallery.

Everything the gallery shows comes out of the host simulator: the effect list and
its parsed defaults from `wled_fx_sim --list-meta`, and one animated preview per
effect from `wled_fx_sim --anim`, which writes raw RGB24 frames that ffmpeg turns
into an animated WebP.

    python tools/gallery/build_gallery.py

With no arguments it configures and builds the simulator into
tools/sim/build-gallery, renders every registered effect, writes
docs/gallery/effects.json and docs/gallery/previews/, and prints a size report.
Raw frames go to a temporary directory and are deleted; they are never committed.

Needs cmake, ninja, a C++17 compiler and ffmpeg with libwebp on PATH.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "components" / "wled_fx"))
from effect_index import flag_overrides  # noqa: E402

SIM_SRC = REPO / "tools" / "sim"
OUT_DIR = REPO / "docs" / "gallery"
PREVIEW_DIR = OUT_DIR / "previews"

# The canvas each preview is rendered on. A 2D capable effect gets a small panel;
# an effect that only runs in 1D gets a strip of the same width, so the LED pitch
# is identical everywhere in the grid and the two kinds of preview sit together.
PANEL = (64, 32)
STRIP = (64, 1)

# 100 frames at 20 fps, which is what --anim renders. The page plays them back at
# the rate baked into the file.
FPS = 20

# Animated WebP quality. 85 was chosen by eye against the raw frames on Fire 2012,
# the noisiest effect in the set: 75 visibly smears the flame tips and 95 costs
# half as much again for no visible gain. Previews are encoded at the canvas
# resolution and upscaled by the page, so a block artefact here would be eight
# LEDs wide and is very easy to see.
WEBP_QUALITY = 85
STILL_QUALITY = 90

# The whole gallery has to stay small enough to live in the repository and load
# on a phone. The build fails rather than quietly growing past this.
BUDGET = 60 * 1024 * 1024

# Palette 0 means "use the segment colours", which is faithful WLED behaviour and
# leaves a palette driven effect rendering as a flat amber field (PORTING.md
# section 6 says the same thing about Metaballs). These effects are previewed on a
# palette instead. The number is a WLED palette ID; the comment is its name.
PALETTE_OVERRIDE: dict[str, int] = {
    "Metaballs": 46,   # April Night. On palette 0 this is a flat amber field.
    "Hiphotic": 11,    # Rainbow, which is the look the effect is known for.
    "Julia": 13,       # Sunset. Palette 0 renders the escape bands in one colour.
    "Fill Noise": 11,  # Rainbow. The noise field is invisible in one colour.
    "Noise Pal": 11,   # Rainbow. Palette 0 leaves this one nearly black.
    "Blobs": 13,       # Sunset, so the blobs are not all the same amber.
}

# Which chip an effect's registry group belongs to. The groups themselves are
# translation units, so the batch letters mean nothing to somebody picking an
# effect.
FAMILY = {
    "1d_a": "1D",
    "1d_b": "1D",
    "1d_c": "1D",
    "1d_d": "1D",
    "1d_e": "1D",
    "2d_a": "2D",
    "2d_b": "2D",
    "1d2d": "1D and 2D",
    "audio_fft": "Audio reactive",
    "audio_vol": "Audio reactive",
    "audio_particle": "Audio reactive",
    "particle_1d": "Particle",
    "particle_2d": "Particle",
    "mm": "WLED-MM",
}

# WLED writes '!' in a metadata field to mean "the default label for this slot".
SLIDER_DEFAULTS = ["Speed", "Intensity", "Custom 1", "Custom 2", "Custom 3"]
COLOR_DEFAULTS = ["Colour 1", "Colour 2", "Colour 3"]
CHECK_DEFAULTS = ["Check 1", "Check 2", "Check 3"]
CONTROL_KEYS = ["speed", "intensity", "custom1", "custom2", "custom3"]

PALETTE_NAMES = [
    "Default", "* Random Cycle", "* Color 1", "* Colors 1&2", "* Color Gradient",
    "* Colors Only", "Party", "Cloud", "Lava", "Ocean", "Forest", "Rainbow",
    "Rainbow Bands", "Sunset", "Rivendell", "Breeze", "Red & Blue", "Yellowout",
    "Analogous", "Splash", "Pastel", "Sunset 2", "Beach", "Vintage", "Departure",
    "Landscape", "Beech", "Sherbet", "Hult", "Hult 64", "Drywet", "Jul",
    "Grintage", "Rewhi", "Tertiary", "Fire", "Icefire", "Cyane", "Light Pink",
    "Autumn", "Magenta", "Magred", "Yelmag", "Yelblu", "Orange & Teal", "Tiamat",
    "April Night", "Orangery", "C9", "Sakura", "Aurora", "Atlantica", "C9 2",
    "C9 New", "Temperature", "Aurora 2", "Retro Clown", "Candy", "Toxy Reaf",
    "Fairy Reaf", "Semi Blue", "Pink Candy", "Red Reaf", "Aqua Flash",
    "Yelblu Hot", "Lite Light", "Red Flash", "Blink Red", "Red Shift", "Red Tide",
    "Candy2", "Traffic Light",
]


def sim_binary(build_dir: Path) -> Path:
    name = "wled_fx_sim.exe" if os.name == "nt" else "wled_fx_sim"
    return build_dir / name


def build_sim(build_dir: Path) -> Path:
    binary = sim_binary(build_dir)
    subprocess.run(
        ["cmake", "-S", str(SIM_SRC), "-B", str(build_dir), "-G", "Ninja"],
        check=True,
    )
    subprocess.run(["cmake", "--build", str(build_dir)], check=True)
    if not binary.exists():
        raise SystemExit(f"the simulator did not appear at {binary}")
    return binary


def split_fields(text: str) -> list[str]:
    return [f.strip() for f in text.split(",")]


def parse_metadata(group: str, metadata: str) -> dict:
    """Read one WLED metadata string the way wf_registry.cpp reads it.

    Name@slider0,..,slider4,check1,check2,check3;colour0,colour1,colour2;palette;flags;defaults
    """
    head, _, rest = metadata.partition(";")
    name, _, controls = head.partition("@")
    groups = rest.split(";") if rest else []

    def group_at(index: int) -> str:
        return groups[index] if index < len(groups) else ""

    fields = split_fields(controls) if controls else []
    sliders = (fields[:5] + [""] * 5)[:5]
    checks = (fields[5:8] + [""] * 3)[:3]
    colors = split_fields(group_at(0)) if group_at(0) else []

    flag_text = group_at(2)
    flags = {
        "d0": "0" in flag_text,
        "d1": "1" in flag_text,
        "d2": "2" in flag_text,
        "volume": "v" in flag_text,
        "fft": "f" in flag_text,
    }
    if not (flags["d0"] or flags["d1"] or flags["d2"]):
        flags["d1"] = True  # an empty flag group means 1D, as the parser does
    # A metadata string with no dimensionality group at all gets its answer from
    # the override table in wf_registry.cpp instead. Reading that table rather
    # than repeating it is what stops the gallery telling somebody an effect
    # runs somewhere the firmware will not offer it.
    if (override := flag_overrides().get(name.casefold())) is not None:
        flags["d0"] = bool(override & (1 << 0))
        flags["d1"] = bool(override & (1 << 1))
        flags["d2"] = bool(override & (1 << 2))

    # The defaults live in the LAST group, which is how WLED reads them too.
    defaults = {
        "speed": 128, "intensity": 128, "custom1": 128, "custom2": 128,
        "custom3": 16, "check1": False, "check2": False, "check3": False,
        "palette": 0, "m12": 0, "si": 0,
    }
    palette_group = group_at(1)
    if palette_group[:1].isdigit():
        defaults["palette"] = int("".join(c for c in palette_group if c.isdigit()))
    keys = {
        "sx": "speed", "ix": "intensity", "c1": "custom1", "c2": "custom2",
        "c3": "custom3", "pal": "palette", "m12": "m12", "si": "si",
        "o1": "check1", "o2": "check2", "o3": "check3",
    }
    for item in groups[-1].split(",") if groups else []:
        key, _, value = item.partition("=")
        target = keys.get(key.strip())
        if target is None or not value.strip().lstrip("-").isdigit():
            continue
        number = int(value.strip())
        defaults[target] = bool(number) if target.startswith("check") else number

    def label(raw: str, fallback: str) -> str | None:
        if raw == "":
            return None  # the control is hidden for this effect
        return fallback if raw == "!" else raw

    entry = {
        "name": name,
        "group": group,
        "family": FAMILY.get(group, group),
        "flags": flags,
        "particle": group.startswith("particle") or group == "audio_particle",
        "audio": flags["volume"] or flags["fft"],
        "defaults": defaults,
        "palette_name": PALETTE_NAMES[defaults["palette"]]
        if defaults["palette"] < len(PALETTE_NAMES)
        else str(defaults["palette"]),
        "sliders": [
            {"key": CONTROL_KEYS[i], "label": label(raw, SLIDER_DEFAULTS[i]),
             "default": defaults[CONTROL_KEYS[i]],
             "max": 31 if CONTROL_KEYS[i] == "custom3" else 255}
            for i, raw in enumerate(sliders)
            if label(raw, SLIDER_DEFAULTS[i]) is not None
        ],
        "checks": [
            {"key": f"check{i + 1}", "label": label(raw, CHECK_DEFAULTS[i]),
             "default": defaults[f"check{i + 1}"]}
            for i, raw in enumerate(checks)
            if label(raw, CHECK_DEFAULTS[i]) is not None
        ],
        "colors": [
            label(raw, COLOR_DEFAULTS[i])
            for i, raw in enumerate(colors[:3])
            if label(raw, COLOR_DEFAULTS[i]) is not None
        ],
    }
    return entry


def list_effects(binary: Path) -> list[dict]:
    text = subprocess.run([str(binary), "--list-meta"], check=True,
                          capture_output=True, text=True).stdout
    entries = []
    for line in text.splitlines():
        if not line.strip():
            continue
        group, _, metadata = line.partition("\t")
        entries.append(parse_metadata(group.strip(), metadata.strip()))
    return entries


def render_one(binary: Path, entry: dict, raw_dir: Path) -> dict:
    """Render one effect's frames and encode them. Returns the preview record."""
    width, height = PANEL if entry["flags"]["d2"] else STRIP
    command = [str(binary), "--effect", entry["name"], "--size", f"{width}x{height}",
               "--anim", str(raw_dir)]
    palette = PALETTE_OVERRIDE.get(entry["name"])
    if palette is not None:
        command += ["--palette", str(palette)]
    proc = subprocess.run(command, capture_output=True, text=True)
    manifest = [l for l in proc.stdout.splitlines() if l.startswith("anim\t")]
    if not manifest:
        raise RuntimeError(f"{entry['name']}: no frames written\n{proc.stdout}\n{proc.stderr}")
    _, raw_path, _, _, frames, _ = manifest[0].split("\t")
    raw = Path(raw_path)
    stem = raw.stem
    animated = PREVIEW_DIR / f"{stem}.webp"
    still = PREVIEW_DIR / f"{stem}.still.webp"

    source = ["-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{width}x{height}",
              "-r", str(FPS), "-i", str(raw)]
    run_ffmpeg(source + ["-loop", "0", "-c:v", "libwebp_anim", "-q:v", str(WEBP_QUALITY),
                         "-compression_level", "6", str(animated)])
    # The still the page shows before a preview is played, and the only thing it
    # shows under prefers-reduced-motion. Frame 10 rather than frame 0, because
    # half a second in is more representative than the opening frame.
    run_ffmpeg(source + ["-vf", "select=eq(n\\,10)", "-frames:v", "1", "-c:v", "libwebp",
                         "-q:v", str(STILL_QUALITY), str(still)])

    brightness, colours = frame_stats(raw, width, height)
    raw.unlink()
    return {
        "stem": stem,
        "w": width,
        "h": height,
        "frames": int(frames),
        "palette_preview": palette,
        "bytes": animated.stat().st_size + still.stat().st_size,
        "brightness": brightness,
        "colours": colours,
        "notes": [l for l in proc.stderr.splitlines() if l.startswith("note")],
    }


def run_ffmpeg(args: list[str]) -> None:
    proc = subprocess.run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error"] + args,
                          capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError("ffmpeg failed: " + proc.stderr.strip())


def frame_stats(raw: Path, width: int, height: int) -> tuple[float, int]:
    """Mean brightness and the number of distinct coarse colours, sampled.

    Cheap enough to run on every effect and it is what finds the previews worth a
    second look: a dark one and a one-colour one both come out near the bottom.
    """
    data = raw.read_bytes()
    stride = 3 * 37  # a prime stride, so the sample is not aligned to the canvas
    total = 0
    seen = set()
    count = 0
    for i in range(0, len(data) - 3, stride):
        r, g, b = data[i], data[i + 1], data[i + 2]
        total += r + g + b
        seen.add((r >> 5, g >> 5, b >> 5))
        count += 1
    return (total / (count * 3 * 255) if count else 0.0), len(seen)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", default=str(SIM_SRC / "build-gallery"),
                        help="where the simulator is built (default tools/sim/build-gallery)")
    parser.add_argument("--sim", help="use an already built simulator binary")
    parser.add_argument("--jobs", type=int, default=max(2, (os.cpu_count() or 4) - 1))
    parser.add_argument("--only", action="append", default=[],
                        help="render just these effects, for iterating on one preview")
    parser.add_argument("--no-clean", action="store_true",
                        help="keep previews that no longer belong to a registered effect")
    args = parser.parse_args()

    if shutil.which("ffmpeg") is None:
        raise SystemExit("ffmpeg is not on PATH")

    binary = Path(args.sim) if args.sim else build_sim(Path(args.build_dir))
    effects = list_effects(binary)
    print(f"{len(effects)} effect(s) registered")

    PREVIEW_DIR.mkdir(parents=True, exist_ok=True)
    selected = [e for e in effects if not args.only or e["name"] in args.only]
    if args.only and len(selected) != len(args.only):
        missing = set(args.only) - {e["name"] for e in selected}
        raise SystemExit(f"not registered: {', '.join(sorted(missing))}")

    raw_dir = Path(tempfile.mkdtemp(prefix="wled-fx-gallery-"))
    failures = []
    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futures = {pool.submit(render_one, binary, e, raw_dir): e for e in selected}
            done = 0
            for future in concurrent.futures.as_completed(futures):
                entry = futures[future]
                done += 1
                try:
                    entry["preview"] = future.result()
                except Exception as error:  # one bad effect must not lose the run
                    failures.append(f"{entry['name']}: {error}")
                    continue
                print(f"  [{done:3d}/{len(selected)}] {entry['name']}", flush=True)
    finally:
        shutil.rmtree(raw_dir, ignore_errors=True)

    if failures:
        for line in failures:
            print("FAILED " + line, file=sys.stderr)
        return 1

    if args.only:
        print("rendered a subset, leaving effects.json alone")
        return 0

    write_json(effects)
    total = report(effects, args.no_clean)
    if total > BUDGET:
        print(f"the gallery is {total / 1e6:.1f} MB, over the {BUDGET / 1e6:.0f} MB budget",
              file=sys.stderr)
        return 1
    return 0


def write_json(effects: list[dict]) -> None:
    payload = {
        "generated_by": "tools/gallery/build_gallery.py",
        "preview": {"fps": FPS, "panel": list(PANEL), "strip": list(STRIP)},
        "families": sorted({e["family"] for e in effects}),
        "effects": [
            {
                "name": e["name"],
                "group": e["group"],
                "family": e["family"],
                "dims": [d for d, on in (("1D", e["flags"]["d1"]), ("2D", e["flags"]["d2"]),
                                         ("0D", e["flags"]["d0"])) if on],
                # Which shape of output offers this effect without being asked.
                # A 1D-only effect is not in "2D" here: a matrix can run it, but
                # only after include_1d_effects, which "optIn" says.
                "offeredOn": [
                    layout
                    for layout, on in (
                        ("1D", e["flags"]["d1"] or e["flags"]["d0"]),
                        ("2D", e["flags"]["d2"]),
                    )
                    if on
                ],
                "optIn": None if e["flags"]["d2"] else "include_1d_effects",
                "audio": ("FFT" if e["flags"]["fft"] else "volume") if e["audio"] else None,
                "particle": e["particle"],
                "palette": e["defaults"]["palette"],
                "paletteName": e["palette_name"],
                "speed": e["defaults"]["speed"],
                "intensity": e["defaults"]["intensity"],
                "m12": e["defaults"]["m12"],
                "sliders": e["sliders"],
                "checks": e["checks"],
                "colors": e["colors"],
                "preview": {
                    "stem": e["preview"]["stem"],
                    "w": e["preview"]["w"],
                    "h": e["preview"]["h"],
                    "palette": e["preview"]["palette_preview"],
                    "paletteName": PALETTE_NAMES[e["preview"]["palette_preview"]]
                    if e["preview"]["palette_preview"] is not None
                    else None,
                },
            }
            for e in effects
        ],
    }
    (OUT_DIR / "effects.json").write_text(json.dumps(payload, indent=1) + "\n",
                                          encoding="utf-8")


def report(effects: list[dict], keep_stale: bool) -> int:
    wanted = set()
    for e in effects:
        wanted.add(f"{e['preview']['stem']}.webp")
        wanted.add(f"{e['preview']['stem']}.still.webp")
    stale = [p for p in PREVIEW_DIR.iterdir() if p.name not in wanted]
    for path in stale:
        print(f"stale preview: {path.name}" + ("" if keep_stale else " (removed)"))
        if not keep_stale:
            path.unlink()

    total = sum(p.stat().st_size for p in PREVIEW_DIR.iterdir())
    biggest = sorted(effects, key=lambda e: -e["preview"]["bytes"])[:10]
    dullest = sorted(effects, key=lambda e: (e["preview"]["colours"],
                                             e["preview"]["brightness"]))[:12]
    print(f"\n{len(effects)} previews, {total / 1e6:.1f} MB total, "
          f"{total / len(effects) / 1024:.0f} KB average")
    print("largest:  " + ", ".join(
        f"{e['name']} {e['preview']['bytes'] / 1024:.0f}K" for e in biggest))
    print("flattest: " + ", ".join(
        f"{e['name']} ({e['preview']['colours']} colours, "
        f"{e['preview']['brightness'] * 100:.0f}% lit)" for e in dullest))
    for e in effects:
        for note in e["preview"]["notes"]:
            print("  " + note)
    return total


if __name__ == "__main__":
    sys.exit(main())
