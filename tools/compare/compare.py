#!/usr/bin/env python3
"""Compare what this port renders against what a real WLED device rendered.

Two capture folders go in, one from ``tools/reference/capture_wled.py`` on a
real device and one from ``tools/snapshot/capture_port.py`` on this port, and a
report comes out: a side-by-side image per effect, a metrics diff, a ranked
REPORT.md with the worst behavioural differences first, and an index.html to
scroll.

**Never expect pixel equality.** Every effect here is seeded from a random
number generator and paced by wall-clock time, and the two sides were captured
minutes apart on different hardware. Two runs of the same effect on the same
device do not agree pixel for pixel either. What is compared is behaviour: is it
moving, which way, how fast, how much of the frame is lit, what colours are in
it, how symmetric it is.

Normalisation, which has to happen before any of that means anything:

* WLED's live view subsamples a 64x64 matrix to 32x32 by taking every second
  pixel. The port's 64x64 frames are subsampled the same way, rather than the
  reference being scaled up, because scaling up invents detail that WLED
  already threw away.
* ESPHome's snapshot display stores RGB565. The reference's 8 bit values go
  through the same quantisation, so the colour depth the harness imposes is not
  reported as a colour difference on all 223 effects.
* Both sides are the raw render buffer: no gamma, no brightness scaling.

Three ways to run it:

    compare.py --reference REF --port PORT --out OUT
    compare.py --reference REF --engine ENG --out OUT      # engine only
    compare.py --reference REF --port PORT --engine ENG --out OUT

The third is the useful one. When the port disagrees with the device and the
plain engine simulator agrees with the device, the fault is in the ESPHome
display front end; when both disagree the same way, it is in the engine. The
report says which for every effect it can.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))

from metrics import (  # noqa: E402
    REFERENCE_VIEW,
    compute_metrics,
    hue_distance,
    quantise_565,
    subsample,
)

# How far off each thing has to be before it is worth a human's attention.
SPEED_RATIO_TOLERANCE = 0.25  # the task's "off by more than about 25 percent"
HUE_DISTANCE_LIMIT = 0.35
COVERAGE_LIMIT = 0.25
BRIGHTNESS_LIMIT = 40.0  # out of 255

# Effects whose comparison is loose by construction: the device has a real
# microphone listening to a real room and the port runs WLED's simulateSound().
# Identified from the flags the registry parses, not from a list kept by hand.
AUDIO_FLAGS = (1 << 3) | (1 << 4)


def log(msg: str) -> None:
    print(msg, flush=True)


# ---------------------------------------------------------------------------
# Loading a capture folder
# ---------------------------------------------------------------------------


class Capture:
    """One effect's frames and metadata from one side, already normalised."""

    def __init__(self, name: str, folder: Path, frames: np.ndarray, timestamps: np.ndarray,
                 meta: dict):
        self.name = name
        self.folder = folder
        self.frames = frames
        self.timestamps = timestamps
        self.meta = meta
        self.metrics = compute_metrics(frames, timestamps)

    @property
    def duration(self) -> float:
        return float(self.timestamps[-1]) if len(self.timestamps) else 0.0


def load_side(root: Path, quantise: bool, label: str) -> tuple[dict[str, Capture], list[str]]:
    """Every effect in a capture folder, normalised to the reference's view.

    Returns the captures keyed by effect name, and the names that were present
    but unusable. A partly filled folder is expected: the reference capture
    takes half an hour and this is meant to be run against it while it is still
    going.
    """
    captures: dict[str, Capture] = {}
    skipped: list[str] = []
    cap_dir = root / "captures"
    if not cap_dir.is_dir():
        log(f"  {label}: no captures/ directory under {root}")
        return captures, skipped

    for d in sorted(cap_dir.iterdir()):
        meta_path, npz_path = d / "meta.json", d / "frames.npz"
        if not meta_path.exists():
            continue
        try:
            meta = json.loads(meta_path.read_text())
        except Exception:  # noqa: BLE001
            skipped.append(d.name)
            continue
        name = meta.get("name")
        if not name:
            skipped.append(d.name)
            continue
        if meta.get("status") == "failed" or not npz_path.exists():
            skipped.append(name)
            continue
        try:
            with np.load(npz_path) as data:
                frames = data["frames"]
                timestamps = data["timestamps"]
        except Exception:  # noqa: BLE001
            # Half written, because the other agent is still capturing into it.
            skipped.append(name)
            continue
        if frames.size == 0:
            skipped.append(name)
            continue

        try:
            frames = subsample(frames, *REFERENCE_VIEW)
        except ValueError as e:
            log(f"  {label}: {name}: {e}")
            skipped.append(name)
            continue
        if quantise:
            frames = quantise_565(frames)
        captures[name] = Capture(name, d, frames, np.asarray(timestamps, dtype=np.float64), meta)
    return captures, skipped


def check_metrics_agree(captures: dict[str, Capture], root: Path) -> list[str]:
    """Does this module reproduce the numbers the reference tool wrote?

    The metric functions in metrics.py are the reference capture tool's, copied
    across a branch boundary. That claim is only worth anything if it is
    checked, so here it is checked: recompute from the reference's own raw
    frames, at its own resolution and with no quantisation, and compare against
    the meta.json it wrote at capture time. A mismatch means the two have
    drifted and every number in this report is suspect.
    """
    problems = []
    for name, cap in list(captures.items())[:40]:  # a sample is enough to catch drift
        stored = cap.meta.get("metrics")
        if not stored or "mean_brightness" not in stored:
            continue
        npz = cap.folder / "frames.npz"
        try:
            with np.load(npz) as data:
                raw, ts = data["frames"], data["timestamps"]
        except Exception:  # noqa: BLE001
            continue
        fresh = compute_metrics(raw, np.asarray(ts, dtype=np.float64))
        for key in ("mean_brightness", "fraction_lit", "mean_frame_change"):
            a, b = stored.get(key), fresh.get(key)
            if a is None or b is None:
                continue
            if abs(float(a) - float(b)) > 1e-6:
                problems.append(f"{name}: {key} stored {a}, recomputed {b}")
    if problems:
        log(f"WARNING: tools/compare/metrics.py does not reproduce {root}'s own numbers:")
        for p in problems[:5]:
            log(f"  {p}")
    return problems


# ---------------------------------------------------------------------------
# Comparing
# ---------------------------------------------------------------------------


# The eight compass points estimate_motion() reports, in order round the circle.
_OCTANTS = ["right", "up-right", "up", "up-left", "left", "down-left", "down", "down-right"]


def octant_distance(a: str, b: str) -> int:
    """How many 45 degree steps apart two reported directions are, 0 to 4."""
    try:
        i, j = _OCTANTS.index(a), _OCTANTS.index(b)
    except ValueError:
        return 0
    step = abs(i - j) % 8
    return min(step, 8 - step)


def speed_ratio(a: float, b: float) -> float | None:
    """b relative to a, or None when neither side is really moving."""
    if a < 1.0 and b < 1.0:
        return None
    if a < 1.0 or b < 1.0:
        return float("inf")
    return b / a


def compare_one(ref: Capture, port: Capture, audio: bool) -> dict:
    rm, pm = ref.metrics, port.metrics
    findings = []
    score = 0.0

    # Ordered worst first, which is also the order the task asks the report to
    # rank in. The score is what sorts the report; the wording is what a person
    # reads.
    if rm["frozen"] != pm["frozen"]:
        moving, still = ("WLED", "the port") if pm["frozen"] else ("the port", "WLED")
        findings.append(f"{moving} animates and {still} does not")
        score += 100

    if rm["all_black"] != pm["all_black"]:
        lit, dark = ("WLED", "the port") if pm["all_black"] else ("the port", "WLED")
        findings.append(f"{lit} renders something and {dark} is black")
        score += 100

    rdir = rm["motion"]["direction"]
    pdir = pm["motion"]["direction"]
    confident = rm["motion"].get("confidence", 0) > 3 and pm["motion"].get("confidence", 0) > 3
    if confident and rdir != pdir and "none/unclear" not in (rdir, pdir):
        # The estimator reports one of eight compass points, so two readings 45
        # degrees apart can be the same motion landing either side of a
        # boundary. Neighbours are noted and scored low; anything further is a
        # real disagreement, and opposite is the interesting one.
        turn = octant_distance(rdir, pdir)
        if turn <= 1:
            findings.append(
                f"motion goes {rdir} on WLED and {pdir} on the port, one step "
                "apart, which is inside the estimator's own resolution"
            )
            score += 8
        elif turn == 4:
            findings.append(f"motion is opposite: {rdir} on WLED, {pdir} on the port")
            score += 90
        else:
            findings.append(f"motion goes {rdir} on WLED and {pdir} on the port")
            score += 60

    ratio = speed_ratio(rm["motion"]["speed_px_per_s"], pm["motion"]["speed_px_per_s"])
    if ratio is not None and confident:
        if ratio == float("inf"):
            findings.append("one side moves and the other is still")
            score += 40
        elif abs(ratio - 1.0) > SPEED_RATIO_TOLERANCE:
            findings.append(
                f"speed is {ratio:.2f}x WLED's "
                f"({rm['motion']['speed_px_per_s']:.1f} vs {pm['motion']['speed_px_per_s']:.1f} px/s)"
            )
            score += 30 * min(3.0, abs(ratio - 1.0))

    hue = hue_distance(rm["dominant_hue_histogram_12bin"], pm["dominant_hue_histogram_12bin"])
    if hue > HUE_DISTANCE_LIMIT:
        findings.append(f"the colours are far apart, hue distance {hue:.2f}")
        score += 40 * hue

    coverage = abs(rm["fraction_lit"] - pm["fraction_lit"])
    if coverage > COVERAGE_LIMIT:
        findings.append(
            f"coverage differs: {rm['fraction_lit']:.0%} of WLED's frame is lit "
            f"and {pm['fraction_lit']:.0%} of the port's"
        )
        score += 30 * coverage

    brightness = abs(rm["mean_brightness"] - pm["mean_brightness"])
    if brightness > BRIGHTNESS_LIMIT:
        findings.append(
            f"mean brightness differs by {brightness:.0f} of 255 "
            f"({rm['mean_brightness']:.0f} vs {pm['mean_brightness']:.0f})"
        )
        score += 20 * (brightness / 255.0)

    return {
        "name": ref.name,
        "score": round(score, 2),
        "findings": findings,
        "audio": audio,
        "hue_distance": round(hue, 3),
        "speed_ratio": None if ratio in (None, float("inf")) else round(ratio, 3),
        "coverage_delta": round(coverage, 4),
        "brightness_delta": round(brightness, 2),
        "reference": rm,
        "port": pm,
    }


def attribute(ref: Capture, port: Capture, engine: Capture | None) -> str:
    """Engine or front end, when there is an engine capture to tell them apart."""
    if engine is None:
        return "unknown, no engine capture"
    port_result = compare_one(ref, port, False)
    engine_result = compare_one(ref, engine, False)
    port_bad = port_result["score"] > 20
    engine_bad = engine_result["score"] > 20
    if port_bad and engine_bad:
        return "engine: the plain simulator disagrees with WLED the same way"
    if port_bad and not engine_bad:
        return "ESPHome front end: the plain simulator matches WLED and this does not"
    if not port_bad and engine_bad:
        return "front end differs from the engine, and matches WLED; look at the simulator"
    return "both agree with WLED"


# ---------------------------------------------------------------------------
# Pictures
# ---------------------------------------------------------------------------


def pick_frames(cap: Capture, count: int, duration: float) -> list[int]:
    """Frame indices at evenly spaced points in TIME, not in the frame list.

    The two sides ran at different frame rates, so taking every n-th frame
    would put the two rows of a side-by-side at different moments and make an
    identical animation look out of step.
    """
    if len(cap.timestamps) == 0:
        return []
    targets = np.linspace(0.0, duration, count)
    return [int(np.argmin(np.abs(cap.timestamps - t))) for t in targets]


def side_by_side(rows: list[tuple[str, Capture]], out_path: Path, count: int = 8) -> None:
    """One row per side, the same eight moments in time across each."""
    duration = min((c.duration for _, c in rows if c.duration > 0), default=0.0)
    h, w = rows[0][1].frames.shape[1], rows[0][1].frames.shape[2]
    scale = max(1, 128 // max(h, w))
    tw, th = w * scale, h * scale
    pad, label_w, header = 4, 78, 16

    width = label_w + count * (tw + pad) + pad
    height = header + len(rows) * (th + pad) + pad
    sheet = Image.new("RGB", (width, height), (18, 18, 18))
    draw = ImageDraw.Draw(sheet)

    for k in range(count):
        t = duration * k / max(1, count - 1)
        draw.text((label_w + k * (tw + pad) + 2, 3), f"{t:0.1f}s", fill=(170, 170, 170))

    for r, (label, cap) in enumerate(rows):
        y = header + r * (th + pad)
        draw.text((4, y + th // 2 - 4), label, fill=(230, 230, 230))
        for k, idx in enumerate(pick_frames(cap, count, duration)):
            tile = Image.fromarray(cap.frames[idx], "RGB").resize((tw, th), Image.NEAREST)
            sheet.paste(tile, (label_w + k * (tw + pad), y))
    sheet.save(out_path)


# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------


def write_report(results: list[dict], out: Path, ref_only: list[str], port_only: list[str],
                 skipped_ref: list[str], skipped_port: list[str], metric_problems: list[str],
                 engine_used: bool) -> None:
    ranked = sorted([r for r in results if not r["audio"]], key=lambda r: -r["score"])
    audio = sorted([r for r in results if r["audio"]], key=lambda r: -r["score"])
    flagged = [r for r in ranked if r["findings"]]

    lines = [
        "# esphome-wled-fx against a real WLED 16.0.1 device",
        "",
        f"{len(results)} effects were captured on both sides. {len(flagged)} of the "
        f"{len(ranked)} non-audio effects have something worth looking at, worst first.",
        "",
        "Nothing here is a pixel comparison. Both sides are seeded from random "
        "numbers and paced by wall-clock time, so two runs of the same effect on "
        "the same device do not match either. What is compared is behaviour.",
        "",
        "Before anything is measured, the port's 64x64 frames are subsampled to "
        "32x32 by taking every second pixel, which is what WLED's live view does, "
        "and the reference's 8 bit pixels are rounded to RGB565 and back, which is "
        "what ESPHome's snapshot display does to the port's. Neither side is "
        "gamma corrected or brightness scaled.",
        "",
        "Two things about the motion estimate before you trust a direction. It "
        "reports one of eight compass points, so a reading 45 degrees away can "
        "be the same motion landing either side of a boundary, and those are "
        "called out and scored low. And it is a phase correlation, which on a "
        "periodic pattern cannot tell a shift of d from a shift of -(period - "
        "d): a tartan, a stripe field or a spiral can be reported as moving "
        "the opposite way on one side and be doing nothing of the kind. Open "
        "the side-by-side before believing an \"opposite\" on an effect that "
        "repeats.",
        "",
    ]

    if metric_problems:
        lines += [
            "> **The metric code has drifted.** `tools/compare/metrics.py` did not "
            "reproduce the numbers the reference capture wrote for its own frames, "
            "so the two sides are not being measured the same way and every "
            "number below is suspect. Fix that first.",
            "",
        ]

    if not engine_used:
        lines += [
            "No engine simulator capture was given, so nothing below says whether "
            "a difference is in the effect engine or in the ESPHome display front "
            "end. Run `compare.py` again with `--engine` to find out.",
            "",
        ]

    lines += ["## Ranked differences", ""]
    if not flagged:
        lines.append("Nothing crossed a threshold.")
    for r in flagged:
        lines.append(f"### {r['name']}  ({r['score']:.0f})")
        lines.append("")
        for f in r["findings"]:
            lines.append(f"* {f}")
        if r.get("attribution"):
            lines.append(f"* Attribution: {r['attribution']}")
        lines.append("")
        lines.append(f"  ![{r['name']}](images/{r['slug']}.png)")
        lines.append("")

    lines += [
        "## Audio reactive effects",
        "",
        "Only loosely comparable. The device has a real microphone listening to a "
        "real room; the port has no microphone configured and runs WLED's own "
        "`simulateSound()`. The two are reacting to different sound, so a "
        "difference here is expected and only a frozen or black frame means much.",
        "",
    ]
    for r in audio:
        state = "; ".join(r["findings"]) if r["findings"] else "nothing obvious"
        lines.append(f"* **{r['name']}** ({r['score']:.0f}): {state}")
    lines.append("")

    lines += ["## Effects on one side only", ""]
    lines.append(f"Only on the device ({len(ref_only)}): {', '.join(ref_only) or 'none'}")
    lines.append("")
    lines.append(f"Only in the port ({len(port_only)}): {', '.join(port_only) or 'none'}")
    lines.append("")
    if skipped_ref or skipped_port:
        lines += [
            "## Captures that could not be read",
            "",
            f"Device: {', '.join(skipped_ref) or 'none'}",
            "",
            f"Port: {', '.join(skipped_port) or 'none'}",
            "",
            "A capture still being written shows up here. Run this again when it "
            "has finished.",
            "",
        ]

    (out / "REPORT.md").write_text("\n".join(lines), encoding="utf-8")


def write_index(results: list[dict], out: Path) -> None:
    ranked = sorted(results, key=lambda r: (r["audio"], -r["score"]))
    html = [
        "<!DOCTYPE html><html><head><meta charset='utf-8'>",
        "<title>esphome-wled-fx against WLED</title><style>",
        "body{font-family:system-ui,sans-serif;background:#141414;color:#eee;margin:0;padding:24px;}",
        "h1{font-weight:400;} .e{background:#1f1f1f;border-radius:8px;padding:12px;margin:0 0 14px;}",
        ".e img{width:100%;max-width:1100px;display:block;border-radius:4px;image-rendering:pixelated;}",
        ".n{font-weight:600;font-size:17px;} .s{float:right;color:#e2a04a;font-variant-numeric:tabular-nums;}",
        "ul{margin:6px 0 10px;color:#ccc;} .audio{outline:1px solid #47607a;}",
        ".ok{opacity:.55;} .att{color:#8fb6d8;font-size:13px;}",
        "</style></head><body>",
        "<h1>esphome-wled-fx against a real WLED 16.0.1 device</h1>",
        "<p>Top row is the device, below it the port, and the engine simulator when "
        "there is one. The same eight moments in time across every row. Both sides "
        "are at WLED's live-view resolution and colour depth. Worst first; the "
        "audio reactive effects are at the end and only loosely comparable.</p>",
    ]
    for r in ranked:
        cls = "e" + (" audio" if r["audio"] else "") + ("" if r["findings"] else " ok")
        html.append(f"<div class='{cls}'>")
        html.append(f"<div class='n'>{r['name']}<span class='s'>{r['score']:.0f}</span></div>")
        if r["findings"]:
            html.append("<ul>" + "".join(f"<li>{f}</li>" for f in r["findings"]) + "</ul>")
        if r.get("attribution"):
            html.append(f"<div class='att'>{r['attribution']}</div>")
        html.append(f"<img loading='lazy' src='images/{r['slug']}.png'>")
        html.append("</div>")
    html.append("</body></html>")
    (out / "index.html").write_text("\n".join(html), encoding="utf-8")


# ---------------------------------------------------------------------------


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--reference", required=True, help="the WLED device capture folder")
    ap.add_argument("--port", default=None, help="the snapshot harness capture folder")
    ap.add_argument("--engine", default=None, help="a plain engine simulator capture folder")
    ap.add_argument("--out", required=True, help="where the report goes")
    args = ap.parse_args()

    if not args.port and not args.engine:
        sys.exit("give at least one of --port and --engine to compare against")

    out = Path(args.out)
    (out / "images").mkdir(parents=True, exist_ok=True)

    log("loading captures")
    # The reference is 8 bit and already at live-view resolution, so it only
    # needs the colour-depth half of the normalisation.
    ref, skipped_ref = load_side(Path(args.reference), quantise=True, label="device")
    log(f"  device: {len(ref)} usable, {len(skipped_ref)} not")
    metric_problems = check_metrics_agree(ref, Path(args.reference))

    port, skipped_port = load_side(Path(args.port), quantise=False, label="port") if args.port else ({}, [])
    if args.port:
        log(f"  port: {len(port)} usable, {len(skipped_port)} not")
    engine, skipped_engine = (
        load_side(Path(args.engine), quantise=True, label="engine") if args.engine else ({}, [])
    )
    if args.engine:
        log(f"  engine: {len(engine)} usable, {len(skipped_engine)} not")

    # Whichever side is the thing under test. With only --engine, the engine is.
    subject = port if args.port else engine
    subject_label = "port" if args.port else "engine"

    audio_names = audio_effect_names()
    results = []
    for name in sorted(set(ref) & set(subject)):
        r = compare_one(ref[name], subject[name], name in audio_names)
        r["slug"] = slugify(name)
        if args.port and args.engine and name in engine:
            r["attribution"] = attribute(ref[name], port[name], engine[name])
        rows = [("WLED", ref[name]), (subject_label, subject[name])]
        if args.port and args.engine and name in engine and name in port:
            rows.append(("engine", engine[name]))
        side_by_side(rows, out / "images" / f"{r['slug']}.png")
        results.append(r)

    ref_only = sorted(set(ref) - set(subject))
    port_only = sorted(set(subject) - set(ref))
    write_report(results, out, ref_only, port_only, skipped_ref,
                 skipped_port or skipped_engine, metric_problems, bool(args.engine))
    write_index(results, out)
    (out / "metrics.json").write_text(json.dumps(results, indent=2), encoding="utf-8")

    flagged = sum(1 for r in results if r["findings"] and not r["audio"])
    log(f"compared {len(results)} effects, {flagged} non-audio effects flagged")
    log(f"  {out / 'REPORT.md'}")
    log(f"  {out / 'index.html'}")
    return 0


def slugify(name: str) -> str:
    import re

    return re.sub(r"[^A-Za-z0-9]+", "_", name).strip("_") or "unnamed"


def audio_effect_names() -> set[str]:
    """The effects that read the audio analysis, from the registry's own flags."""
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "components" / "wled_fx"))
    from effect_index import effect_flags

    return {n for n, f in effect_flags().items() if f & AUDIO_FLAGS}


if __name__ == "__main__":
    raise SystemExit(main())
