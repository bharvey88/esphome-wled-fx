#!/usr/bin/env python3
"""Capture what this port renders, in the shape the WLED reference capture uses.

Compiles tools/snapshot/port.yaml once, runs the resulting host binary as one
process per core with a slice of the effect list each, and turns the .bmp files
the ``snapshot`` component writes into the same per-effect folder the reference
produces: ``frames.npz``, ``sheet.png``, ``anim.webp`` and ``meta.json``, with
the metrics computed by ``tools/compare/metrics.py``, which is the reference
tool's own metric code.

Why real time rather than a faked clock
---------------------------------------
The ESPHome host platform runs in real time and the effects read a real
``millis()``. A test-only time source would have to reach into either the
component's frame gate or ESPHome's own clock, and at that point the harness is
no longer measuring the code that ships, which is the one thing it exists to do.
Real time costs about 7.5 s an effect; sixteen processes turn 223 of those into
under two minutes of wall clock, which is cheaper than the dishonesty.

Run it through the WSL wrapper, which knows where the virtualenv is:

    powershell -File tools\\wsl\\wfx.ps1 snapshot

See --help for options.
"""

from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

import numpy as np
from PIL import Image, ImageDraw

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "components" / "wled_fx"))
sys.path.insert(0, str(REPO / "tools" / "compare"))

from effect_index import effect_names  # noqa: E402
from metrics import compute_metrics  # noqa: E402

CONFIG = REPO / "tools" / "snapshot" / "port.yaml"
BINARY = CONFIG.parent / ".esphome" / "build" / "wfx-snap" / ".pioenvs" / "wfx-snap" / "program"
DEFAULT_OUT = REPO.parent / "esphome-wled-fx-port"

# The panel the reference device is, so the two are the same shape before
# anything is downsampled.
CANVAS = (64, 64)


def log(msg: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)


def safe_folder_name(name: str, index: int) -> str:
    """The reference tool's naming, with our registry index in place of WLED's id.

    The two sides number their effects differently, so the folder names do not
    line up and the comparison joins on the name in meta.json instead. The index
    is still in the folder name because it sorts the folders the way the
    registry is ordered, which is the order everything else in this repository
    reports effects in.
    """
    slug = re.sub(r"[^A-Za-z0-9]+", "_", name).strip("_") or "unnamed"
    return f"{slug}_{index:03d}"


# ---------------------------------------------------------------------------
# Running the harness
# ---------------------------------------------------------------------------


def compile_harness(esphome: str) -> None:
    log("compiling tools/snapshot/port.yaml")
    subprocess.run([esphome, "compile", str(CONFIG)], check=True, cwd=CONFIG.parent)
    if not BINARY.exists():
        sys.exit(f"the compile succeeded but there is no binary at {BINARY}")


def read_reference_applied(root: Path) -> dict[str, dict]:
    """What a reference capture says the device was actually running.

    WLED's `fxdef: true` leaves the palette alone when the effect's metadata
    names none, so it keeps whatever the previous effect was using, while this
    port puts it back to Default. Capturing a device effect by effect therefore
    carries a palette from one to the next: on the first full run that was 112
    of 214 effects rendering in a different palette from the port for a reason
    that has nothing to do with the port, which would have swamped every real
    difference underneath it.

    Reading the device's own applied state back out and forcing the same values
    here is what makes the rest of the comparison mean anything.
    """
    applied: dict[str, dict] = {}
    captures = root / "captures"
    if not captures.is_dir():
        sys.exit(f"--match-reference {root} has no captures/ directory")
    for d in sorted(captures.iterdir()):
        meta_path = d / "meta.json"
        if not meta_path.exists():
            continue
        try:
            meta = json.loads(meta_path.read_text())
        except Exception:  # noqa: BLE001
            continue
        fields = meta.get("applied_state_fields")
        if not fields or not meta.get("name"):
            continue
        applied[meta["name"]] = fields
    return applied


def write_apply_file(applied: dict[str, dict], path: Path) -> int:
    """The tab separated file snaphelp.h reads. -1 means leave it alone."""
    lines = []
    for name, f in sorted(applied.items()):
        def num(key, default=-1):
            v = f.get(key)
            return default if v is None else int(v)

        def flag(key):
            v = f.get(key)
            return -1 if v is None else (1 if v else 0)

        lines.append(
            "\t".join(
                str(x)
                for x in (
                    name, num("pal"), num("sx"), num("ix"),
                    num("c1"), num("c2"), num("c3"),
                    flag("o1"), flag("o2"), flag("o3"),
                )
            )
        )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return len(lines)


def run_workers(effects: list[str], workdir: Path, workers: int, seconds: float,
                settle_ms: int, period_ms: int, apply_file: Path | None = None) -> list[Path]:
    """One process per slice. Returns the worker directories, in slice order."""
    # Round robin rather than contiguous blocks: the slow effects are clustered
    # (the particle groups sit together in the registry), and a contiguous split
    # leaves one worker running long after the rest have finished.
    slices: list[list[str]] = [[] for _ in range(workers)]
    for i, name in enumerate(effects):
        slices[i % workers].append(name)
    slices = [s for s in slices if s]

    procs = []
    dirs = []
    for i, names in enumerate(slices):
        wdir = workdir / f"w{i:02d}"
        if wdir.exists():
            shutil.rmtree(wdir)
        wdir.mkdir(parents=True)
        env = dict(os.environ)
        env.update(
            {
                # Read by the snapshot component itself, which is how one binary
                # writes to as many places as there are processes.
                "ESPHOME_SNAPSHOT_DIR": str(wdir),
                # Two host processes sharing a preferences file would fight.
                "ESPHOME_PREFDIR": str(wdir / "prefs"),
                "WFX_SNAP_EFFECTS": ",".join(names),
                "WFX_SNAP_MANIFEST": str(wdir / "manifest.txt"),
                "WFX_SNAP_SECONDS": str(seconds),
                "WFX_SNAP_SETTLE_MS": str(settle_ms),
                "WFX_SNAP_PERIOD_MS": str(period_ms),
            }
        )
        if apply_file is not None:
            env["WFX_SNAP_APPLY"] = str(apply_file)
        log_path = wdir / "run.log"
        handle = log_path.open("w", encoding="utf-8")
        procs.append(
            (
                subprocess.Popen([str(BINARY)], env=env, stdout=handle, stderr=subprocess.STDOUT),
                handle,
                wdir,
            )
        )
        dirs.append(wdir)

    log(f"{len(procs)} worker(s) running, about {len(slices[0]) * (seconds + settle_ms / 1000 + 0.5):.0f} s")
    failed = []
    for proc, handle, wdir in procs:
        code = proc.wait()
        handle.close()
        if code != 0:
            failed.append((wdir, code))
    for wdir, code in failed:
        log(f"WARNING: worker {wdir.name} exited {code}, see {wdir / 'run.log'}")
    return dirs


# ---------------------------------------------------------------------------
# Reading what the harness wrote
# ---------------------------------------------------------------------------


def read_bmp(path: Path) -> np.ndarray:
    """A 24 bit BMP as an RGB array, topmost row first.

    Pillow reads what the snapshot component writes without any help; this is
    here so a truncated file from an interrupted run is a clear error rather
    than a shape mismatch a hundred lines later.
    """
    with Image.open(path) as img:
        arr = np.asarray(img.convert("RGB"), dtype=np.uint8)
    if arr.shape[:2] != (CANVAS[1], CANVAS[0]):
        raise ValueError(f"{path} is {arr.shape[1]}x{arr.shape[0]}, expected {CANVAS[0]}x{CANVAS[1]}")
    return arr


def read_manifest(wdir: Path) -> dict[str, list[tuple[int, str]]]:
    """Effect name to its frames, as (millis, filename), in capture order."""
    out: dict[str, list[tuple[int, str]]] = {}
    manifest = wdir / "manifest.txt"
    if not manifest.exists():
        return out
    for line in manifest.read_text(encoding="utf-8").splitlines():
        parts = line.split("\t")
        if len(parts) != 5:
            continue
        _, name, _, millis, filename = parts
        out.setdefault(name, []).append((int(millis), filename))
    return out


# ---------------------------------------------------------------------------
# Output artifacts, laid out exactly the way the reference capture lays them out
# ---------------------------------------------------------------------------


def make_contact_sheet(frames: np.ndarray, timestamps, out_path: Path) -> None:
    F = frames.shape[0]
    if F == 0:
        img = Image.new("RGB", (400, 100), (40, 0, 0))
        ImageDraw.Draw(img).text((10, 40), "NO FRAMES CAPTURED", fill=(255, 80, 80))
        img.save(out_path)
        return
    n = min(8, F)
    idxs = sorted({int(round(x)) for x in np.linspace(0, F - 1, n)})
    h, w = frames.shape[1], frames.shape[2]
    scale = max(1, 220 // max(h, w))
    tw, th = w * scale, h * scale
    cols = 4
    rows = math.ceil(len(idxs) / cols)
    pad = 6
    sheet = Image.new("RGB", (cols * tw + (cols + 1) * pad, rows * th + (rows + 1) * pad), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    for k, idx in enumerate(idxs):
        tile = Image.fromarray(frames[idx], "RGB").resize((tw, th), Image.NEAREST)
        c, r = k % cols, k // cols
        x, y = pad + c * (tw + pad), pad + r * (th + pad)
        sheet.paste(tile, (x, y))
        ts = timestamps[idx] if idx < len(timestamps) else 0.0
        draw.text((x + 2, y + 2), f"{ts:0.1f}s", fill=(255, 255, 0))
    sheet.save(out_path)


def make_anim_webp(frames: np.ndarray, timestamps, out_path: Path) -> bool:
    if frames.shape[0] == 0:
        return False
    h, w = frames.shape[1], frames.shape[2]
    scale = max(1, 200 // max(h, w))
    imgs = [Image.fromarray(f, "RGB").resize((w * scale, h * scale), Image.NEAREST) for f in frames]
    avg_dt = float(np.mean(np.diff(timestamps))) if len(timestamps) > 1 else 0.1
    try:
        imgs[0].save(
            out_path, save_all=True, append_images=imgs[1:], duration=max(20, int(avg_dt * 1000)),
            loop=0, format="WEBP", quality=80, method=4,
        )
        return True
    except Exception as e:  # noqa: BLE001
        log(f"  WARNING: anim.webp write failed: {e}")
        return False


def pack_effect(name: str, index: int, entries: list[tuple[int, str]], wdir: Path,
                outdir: Path, applied: dict) -> dict:
    folder = safe_folder_name(name, index)
    effect_dir = outdir / "captures" / folder
    effect_dir.mkdir(parents=True, exist_ok=True)

    meta = {
        "id": index,
        "name": name,
        "folder": folder,
        "source": "esphome-wled-fx display front end via the ESPHome snapshot component",
        "canvas": {"width": CANVAS[0], "height": CANVAS[1]},
        "applied": applied,
        "capture_notes": CAPTURE_NOTES,
    }

    frames_list, times = [], []
    for millis, filename in entries:
        path = wdir / filename
        if not path.exists():
            continue
        try:
            frames_list.append(read_bmp(path))
        except Exception as e:  # noqa: BLE001
            log(f"  WARNING: {name}: {e}")
            continue
        times.append(millis)

    if not frames_list:
        meta["status"] = "failed"
        meta["error"] = "no frames were written"
        (effect_dir / "meta.json").write_text(json.dumps(meta, indent=2))
        make_contact_sheet(np.zeros((0, 1, 1, 3), dtype=np.uint8), [], effect_dir / "sheet.png")
        return meta

    frames = np.stack(frames_list).astype(np.uint8)
    # Seconds from the first captured frame, which is what the reference tool's
    # timestamps are and what the motion estimate divides by.
    ts = (np.array(times, dtype=np.float64) - times[0]) / 1000.0

    metrics = compute_metrics(frames, ts)
    measured_fps = round((len(ts) - 1) / (ts[-1] - ts[0]), 2) if len(ts) > 1 and ts[-1] > ts[0] else 0.0

    meta.update(
        {
            "status": "ok",
            "frame_count": int(frames.shape[0]),
            "measured_fps": measured_fps,
            "received_size": {"width": int(frames.shape[2]), "height": int(frames.shape[1])},
            "metrics": metrics,
        }
    )

    np.savez_compressed(effect_dir / "frames.npz", frames=frames, timestamps=ts)
    make_contact_sheet(frames, ts.tolist(), effect_dir / "sheet.png")
    make_anim_webp(frames, ts.tolist(), effect_dir / "anim.webp")
    (effect_dir / "meta.json").write_text(json.dumps(meta, indent=2))
    return meta


CAPTURE_NOTES = {
    "path": (
        "Frames come off the real wled_fx display front end: YAML, codegen, the "
        "frame gate, Engine::render(), the canvas to RGB888 conversion through "
        "the gamma table, one bulk draw_pixels_at() and the display's own "
        "update(). Only the last step differs from a HUB75 panel."
    ),
    "colour_depth": (
        "ESPHome's snapshot display is a DisplayBuffer, which stores RGB565 and "
        "reads back each channel spread over the full 0 to 255 range. The port "
        "renders 8 bits a channel and this is where the other bits go. "
        "tools/compare/metrics.py puts the reference side through the same "
        "quantisation before comparing, so it is not mistaken for a colour bug."
    ),
    "brightness_and_gamma": (
        "gamma_correct is 1.0, which makes the gamma table the identity, and "
        "nothing dims the master, so these are the engine's own pixel values. "
        "That is what WLED's live view sends too."
    ),
    "resolution": (
        "64x64, the panel size. WLED's live view subsamples to 32x32, so the "
        "comparison tool subsamples these the same way rather than scaling the "
        "reference up."
    ),
    "controls": (
        "Each effect is selected by name, which refills every control from the "
        "effect's own WLED metadata defaults. Nothing is pinned, so that is the "
        "same state WLED's `fxdef: true` produces. The three colour slots are "
        "set to WLED's factory values, amber primary and black in the other two."
    ),
    "audio": (
        "No microphone is configured, so the audio reactive effects run on "
        "WLED's own simulateSound(). The reference device has a real "
        "microphone, so those effects are only loosely comparable."
    ),
    "text": (
        "Scrolling Text is given the string 'WLED FX', because with no text it "
        "has nothing to draw and would look like a dead effect. Whatever the "
        "device is scrolling is its own setting, so that one effect will never "
        "line up character for character."
    ),
}


def build_index(outdir: Path) -> None:
    captures = outdir / "captures"
    entries = []
    for d in sorted(captures.iterdir()) if captures.exists() else []:
        meta_path = d / "meta.json"
        if meta_path.exists():
            try:
                entries.append(json.loads(meta_path.read_text()))
            except Exception:  # noqa: BLE001
                continue
    entries.sort(key=lambda m: m.get("id", 0))
    (outdir / "index.json").write_text(json.dumps(entries, indent=2))
    failed = [e for e in entries if e.get("status") == "failed"]
    black = [e for e in entries if e.get("metrics", {}).get("all_black")]
    frozen = [e for e in entries if e.get("metrics", {}).get("frozen")]
    lines = [
        "# esphome-wled-fx snapshot capture summary",
        "",
        f"Captured {len(entries)} effects through the display front end.",
        "",
        f"## Failures ({len(failed)})",
        *([f"- [{e['id']}] {e['name']}: {e.get('error', 'unknown')}" for e in failed] or ["- none"]),
        "",
        f"## All-black effects ({len(black)})",
        *([f"- [{e['id']}] {e['name']}" for e in black] or ["- none"]),
        "",
        f"## Frozen effects ({len(frozen)})",
        *([f"- [{e['id']}] {e['name']}" for e in frozen] or ["- none"]),
        "",
    ]
    (outdir / "SUMMARY.md").write_text("\n".join(lines))
    log(f"index built: {len(entries)} entries ({len(failed)} failed, {len(black)} black, {len(frozen)} frozen)")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--outdir", default=str(DEFAULT_OUT), help="where the per-effect folders go")
    ap.add_argument("--workdir", default="/root/wfx/snapshots", help="scratch for the .bmp files")
    ap.add_argument("--effects", default=None, help="comma separated names, default every registered effect")
    ap.add_argument("--workers", type=int, default=0, help="processes, default one per core")
    ap.add_argument("--seconds", type=float, default=6.0, help="capture window per effect")
    ap.add_argument("--settle-ms", type=int, default=1500, help="run before capturing")
    ap.add_argument("--period-ms", type=int, default=50, help="between captured frames")
    ap.add_argument(
        "--match-reference",
        default=None,
        help="a reference capture folder to take each effect's applied palette "
        "and controls from, so the two sides run the same configuration",
    )
    ap.add_argument("--esphome", default=str(Path("/root/wfx/esphome-venv/bin/esphome")))
    ap.add_argument("--no-compile", action="store_true", help="use the binary already built")
    ap.add_argument("--keep-bmp", action="store_true", help="leave the scratch .bmp files behind")
    args = ap.parse_args()

    names = effect_names()
    if args.effects:
        wanted = [n.strip() for n in args.effects.split(",") if n.strip()]
        folded = {n.casefold(): n for n in names}
        missing = [n for n in wanted if n.casefold() not in folded]
        if missing:
            sys.exit(f"not registered: {missing}")
        names = [folded[n.casefold()] for n in wanted]

    index_of = {n: i for i, n in enumerate(effect_names())}
    outdir = Path(args.outdir)
    workdir = Path(args.workdir)
    workdir.mkdir(parents=True, exist_ok=True)

    if not args.no_compile:
        compile_harness(args.esphome)
    elif not BINARY.exists():
        sys.exit(f"--no-compile but there is no binary at {BINARY}")

    apply_file = None
    matched: dict[str, dict] = {}
    if args.match_reference:
        matched = read_reference_applied(Path(args.match_reference))
        apply_file = workdir / "applied.tsv"
        count = write_apply_file(matched, apply_file)
        log(f"matching {count} effect(s) to the state the device was in")

    workers = args.workers or min(len(names), os.cpu_count() or 4)
    started = time.time()
    dirs = run_workers(names, workdir, workers, args.seconds, args.settle_ms, args.period_ms,
                       apply_file)
    log(f"capture finished in {time.time() - started:.0f} s, packing")

    applied = {
        "selected_by": "name, which reapplies the effect's WLED metadata defaults (as fxdef: true does)",
        "matched_to_reference": args.match_reference or None,
        "colors": [[255, 160, 0], [0, 0, 0], [0, 0, 0]],
        "gamma_correct": 1.0,
        "master_brightness": 255,
        "frame_interval_ms": 23,
        "capture_seconds": args.seconds,
        "settle_ms": args.settle_ms,
        "capture_period_ms": args.period_ms,
    }

    packed = 0
    seen = set()
    for wdir in dirs:
        for name, entries in read_manifest(wdir).items():
            per_effect = dict(applied)
            if name in matched:
                # Exactly what was forced, so the meta.json says what was run
                # rather than only what would have been run by default.
                per_effect["forced_from_reference"] = matched[name]
            pack_effect(name, index_of.get(name, 999), entries, wdir, outdir, per_effect)
            seen.add(name)
            packed += 1
    missing = [n for n in names if n not in seen]
    if missing:
        log(f"WARNING: no frames at all for {len(missing)} effect(s): {missing[:10]}")

    build_index(outdir)
    if not args.keep_bmp:
        for wdir in dirs:
            shutil.rmtree(wdir, ignore_errors=True)
    log(f"packed {packed} effect(s) into {outdir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
