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
#
# These are measured, not chosen. The whole reference set was captured twice
# from the same device with the same firmware and the same settings, four hours
# apart (`esphome-wled-fx-ref` and `esphome-wled-fx-ref2`), and these are the
# p95 of what those two runs disagree about, per effect class. The working is in
# verification/round-2/NOISE.md. Anything inside them is the instrument, not the
# port: round 2's numbers were 40 counts of brightness, 0.25 of coverage and a
# hue distance of 0.35, three of which sat below the device's disagreement with
# itself, so they could not separate a defect from a second run of WLED.
#
# Re-measure them by capturing the device twice again and rerunning
# tools/compare/compare.py with both runs as --reference and no --port.
NOISE_FLOORS = {
    "other": {
        "mean_brightness": 24.0,
        "fraction_lit": 0.05,
        "mean_frame_change": 2.5,
        "hue_distance": 0.78,
        "speed_ratio": 0.55,
    },
    "particle": {
        "mean_brightness": 8.0,
        "fraction_lit": 0.06,
        "mean_frame_change": 1.4,
        "hue_distance": 0.82,
        "speed_ratio": 0.55,
    },
    # A real microphone in a real room against itself twenty minutes later
    # disagrees by 107 counts of brightness and 0.40 of coverage, so brightness,
    # coverage and frame change are not scorable on an audio effect at all. Only
    # black, frozen for the whole window and the wrong axis mean anything.
    "audio": {
        "mean_brightness": None,
        "fraction_lit": 0.40,
        "mean_frame_change": None,
        "hue_distance": 0.60,
        "speed_ratio": None,
    },
}

# The motion estimator's own confidence has to clear this before a direction or
# a speed is worth reporting. See metrics.estimate_motion.
MOTION_CONFIDENCE_LIMIT = 3.0

# A twelve bin hue histogram built from a handful of pixels swings from one run
# to the next. Round 1 flagged PS Fireworks at a hue distance of 1.00 on about
# fifty lit pixels and Fireworks at 0.41 on twenty-eight. Below this many lit
# pixels a frame the hue reading is reported as a note and not scored.
HUE_MIN_SAMPLES_PER_FRAME = 40

# A reference frame whose darkest pixel is this bright on all three channels is
# showing WLED's uninitialised white channel through the live view's RGBW to RGB
# map, not a brighter render. See TOOLING.md T6 and PORTING.md deviation 28.
WHITE_FLOOR_LIMIT = 12

# Effects whose comparison is loose by construction: the device has a real
# microphone listening to a real room and the port runs WLED's simulateSound().
# Identified from the flags the registry parses, not from a list kept by hand.
AUDIO_FLAGS = (1 << 3) | (1 << 4)


def log(msg: str) -> None:
    print(msg, flush=True)


def margin_text(value: float, floor: float | None) -> str:
    """How far outside the noise floor a flag is, which is the whole point.

    A flag that does not say what it is being compared against is a flag the
    reader has to re-derive.
    """
    if floor is None:
        return "not scorable against a noise floor for this class"
    if floor <= 0:
        return f"floor {floor:.2f}"
    return f"{value / floor:.1f}x the {floor:g} noise floor"


def particle_effect_names() -> set[str]:
    """The `PS *` family, read from the files that register them.

    From the sources rather than from a name prefix, so an effect that is
    renamed or a particle effect that never had `PS ` in its name still lands
    in the class its noise floor was measured on.
    """
    root = Path(__file__).resolve().parents[2] / "components" / "wled_fx"
    names: set[str] = set()
    import re as _re

    pattern = _re.compile(r'\{"([^"\\]+?)(?:@[^"\\]*)?",\s*mode_')
    for source in ("wf_effects_particle_1d.cpp", "wf_effects_particle_2d.cpp",
                   "wf_effects_audio_particle.cpp"):
        path = root / source
        if not path.exists():
            continue
        names |= set(pattern.findall(path.read_text(encoding="utf-8")))
    return names


def effect_class(name: str, audio_names: set[str], particle_names: set[str]) -> str:
    """Which noise floor applies. Audio wins: it is the loosest of the three."""
    if name in audio_names:
        return "audio"
    if name in particle_names:
        return "particle"
    return "other"


# ---------------------------------------------------------------------------
# Loading a capture folder
# ---------------------------------------------------------------------------


# The metrics a repeat capture is allowed to disagree with itself about, and
# which the flag for that metric then has to beat.
SPREAD_KEYS = ("mean_brightness", "fraction_lit", "mean_frame_change")


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
        # Filled in by merge_runs() when the same side was captured more than
        # once. How far two runs of the same effect on the same build disagree
        # is the floor below which a difference between two sides means
        # nothing, and round 1 had no such floor.
        self.spread: dict[str, float] = {}
        self.runs = 1
        # Every run's hue histogram, so the comparison can take the closest
        # pairing rather than the first. An effect that draws a random colour
        # agrees with the other side sometimes; a broken one never does.
        self.hue_runs: list[list[float]] = [self.metrics["dominant_hue_histogram_12bin"]]
        # Every run's window length in seconds. Comparing a 6 s capture against
        # a 30 s one is not a comparison.
        self.durations: list[float] = [self.duration]
        # Every run's motion reading. The direction word disagrees between two
        # runs of the same WLED firmware on 22 of 216 effects, so a single
        # pairing of it is not evidence either.
        self.motion_runs: list[dict] = [self.metrics["motion"]]

    @property
    def duration(self) -> float:
        return float(self.timestamps[-1]) if len(self.timestamps) else 0.0


def merge_runs(runs: list[dict[str, Capture]]) -> dict[str, Capture]:
    """Several captures of one side into one, per-effect median with a spread.

    Every effect here is seeded from a random number generator and most of them
    are paced against a clock that does not restart with the capture, so one
    six second window is a sample and not a measurement. Two runs of Tri Wipe
    on this build measured 200 and 242 counts of mean brightness, and Pride
    2015 measured 114 and 169. A single run treated as ground truth is how a
    report ends up ranking noise.

    The first run supplies the frames for the side-by-side; the metrics become
    the per-effect median across runs and each one carries the full range.
    """
    if len(runs) == 1:
        return runs[0]
    merged: dict[str, Capture] = {}
    for name, first in runs[0].items():
        present = [r[name] for r in runs if name in r]
        first.runs = len(present)
        first.hue_runs = [c.metrics["dominant_hue_histogram_12bin"] for c in present]
        first.durations = [c.duration for c in present]
        first.motion_runs = [c.metrics["motion"] for c in present]
        for key in SPREAD_KEYS:
            values = [float(c.metrics[key]) for c in present if key in c.metrics]
            if not values:
                continue
            first.metrics[key] = round(float(np.median(values)), 3)
            first.spread[key] = round(max(values) - min(values), 3)
        merged[name] = first
    return merged


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


def pooled_hue_distance(ref: Capture, port: Capture) -> tuple[float, float]:
    """The closest and the widest hue distance over every pair of runs.

    Twelve of round 2's twenty-five surviving flags were hue, and every one was
    at or below the same effect's hue distance between two runs of the device.
    Blink Rainbow is the clean illustration: its three port runs score 0.75,
    0.04 and 0.24 against the same device run, and the report used to quote the
    first. What is scored is the minimum, because two runs that ever agree are
    two runs that can agree.
    """
    distances = [hue_distance(a, b) for a in ref.hue_runs for b in port.hue_runs]
    if not distances:
        return 0.0, 0.0
    return min(distances), max(distances)


def internal_hue_spread(cap: Capture) -> float:
    """How far one side's own runs are from each other, in hue distance.

    NOISE.md's third rule: the per-effect spread is the threshold, not a class
    constant. TV Simulator, Wipe Random and Sweep Random each score 1.00
    against themselves between two runs of the same device, because they draw
    a random colour, while PS Vortex agrees with itself to 0.1 counts. A class
    p95 clears one and wrongly flags the other.
    """
    if len(cap.hue_runs) < 2:
        return 0.0
    return max(
        hue_distance(cap.hue_runs[i], cap.hue_runs[j])
        for i in range(len(cap.hue_runs))
        for j in range(i + 1, len(cap.hue_runs))
    )


def compare_one(ref: Capture, port: Capture, cls: str) -> dict:
    audio = cls == "audio"
    floors = NOISE_FLOORS[cls]
    rm, pm = ref.metrics, port.metrics
    findings = []
    # Things a reader needs in order to judge a finding, which are not
    # themselves findings and do not score.
    notes: list[str] = []
    score = 0.0

    # A six second window against a thirty second one measures two different
    # things: where in its cycle a slow effect was caught decides its
    # brightness and its coverage. Those are not scored when the windows
    # differ, and the report says why. Capture both sides with the same
    # SLOW_EFFECTS table and this never fires.
    windows_match = True
    if ref.durations and port.durations:
        shortest = min(min(ref.durations), min(port.durations))
        longest = max(max(ref.durations), max(port.durations))
        if shortest > 0 and longest / shortest > 1.25:
            windows_match = False
            notes.append(
                f"the two sides were captured over different windows, "
                f"{min(ref.durations):.0f} s against {min(port.durations):.0f} s, so "
                "brightness, coverage and frame change are not scored. Recapture the "
                "device side with the same SLOW_EFFECTS window"
            )

    # Ordered worst first, which is also the order the task asks the report to
    # rank in. The score is what sorts the report; the wording is what a person
    # reads.
    if rm["frozen"] != pm["frozen"]:
        moving, still = ("WLED", "the port") if pm["frozen"] else ("the port", "WLED")
        change = (
            f"mean frame change {rm['mean_frame_change']:.2f} against "
            f"{pm['mean_frame_change']:.2f}"
        )
        change_floor = floors["mean_frame_change"]
        gap = abs(rm["mean_frame_change"] - pm["mean_frame_change"])
        if (change_floor is None or not windows_match
                or gap <= max(change_floor, ref.spread.get("mean_frame_change", 0.0),
                              port.spread.get("mean_frame_change", 0.0))):
            # Fifteen of 216 effects flip this boolean between two runs of the
            # same firmware, so on its own it is a coin toss.
            notes.append(
                f"the frozen flag differs ({change}), but that is inside the "
                "noise floor for this class, so it is not scored"
            )
        else:
            findings.append(
                f"{moving} animates and {still} does not: {change}, "
                + margin_text(gap, change_floor)
            )
            score += 100

    if rm["all_black"] != pm["all_black"]:
        lit, dark = ("WLED", "the port") if pm["all_black"] else ("the port", "WLED")
        brightness_gap = abs(rm["mean_brightness"] - pm["mean_brightness"])
        black_floor = floors["mean_brightness"]
        numbers = (
            f"mean brightness {rm['mean_brightness']:.1f} against {pm['mean_brightness']:.1f}"
        )
        if black_floor is not None and brightness_gap <= black_floor:
            # Three of 216 effects flip this between two runs of the same
            # firmware, all of them on frames that are nearly black anyway.
            notes.append(
                f"the all-black flag differs ({numbers}), but that is inside the "
                "noise floor for this class, so it is not scored"
            )
        else:
            findings.append(f"{lit} renders something and {dark} is black ({numbers})")
            score += 100


    rdir = rm["motion"]["direction"]
    pdir = pm["motion"]["direction"]
    # Phase correlation on a frame with a handful of lit pixels answers with
    # whatever the noise did. PS Sparkler measures 0.0 px/s on the device and
    # 4.9 on the port at a mean brightness of 0.3 on both, which is not a
    # difference in the animation. The same sample count the hue reading uses.
    ref_lit = (rm.get("hue_samples_per_frame") or 0.0)
    port_lit = (pm.get("hue_samples_per_frame") or 0.0)
    lit_enough = min(ref_lit, port_lit) >= HUE_MIN_SAMPLES_PER_FRAME
    confident = (
        rm["motion"].get("confidence", 0) > MOTION_CONFIDENCE_LIMIT
        and pm["motion"].get("confidence", 0) > MOTION_CONFIDENCE_LIMIT
        and lit_enough
    )
    if not lit_enough:
        notes.append(
            f"both sides light about {min(ref_lit, port_lit):.0f} pixels a frame, which is too "
            "few for the motion estimate to mean anything, so direction and speed are not scored"
        )
    # Every confident direction word each side produced, so a disagreement has
    # to hold for every pairing of runs before it is scored. The estimator's
    # own word flips between two runs of the same firmware on one effect in
    # ten, which is more often than most of the findings it used to raise.
    def direction_set(cap: Capture) -> set[str]:
        return {
            m["direction"]
            for m in cap.motion_runs
            if m.get("confidence", 0) > MOTION_CONFIDENCE_LIMIT and m["direction"] != "none/unclear"
        }

    ref_dirs, port_dirs = direction_set(ref), direction_set(port)
    directions_disjoint = bool(ref_dirs) and bool(port_dirs) and not (ref_dirs & port_dirs)
    if confident and rdir != pdir and "none/unclear" not in (rdir, pdir) and not directions_disjoint:
        notes.append(
            f"motion reads {rdir} on WLED and {pdir} on the port, but the two "
            f"sides' runs overlap ({sorted(ref_dirs)} against {sorted(port_dirs)}), "
            "and the direction word flips between two runs of the same firmware "
            "on one effect in ten"
        )
    elif confident and rdir != pdir and "none/unclear" not in (rdir, pdir):
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
    speed_floor = floors["speed_ratio"]
    if ratio is not None and confident and speed_floor is not None:
        if ratio == float("inf"):
            # One side measured zero. Four effects do this between two runs of
            # the same device, so it is only a finding when every run on one
            # side moves and every run on the other does not.
            def speeds(cap: Capture) -> list[float]:
                return [
                    m["speed_px_per_s"]
                    for m in cap.motion_runs
                    if m.get("confidence", 0) > MOTION_CONFIDENCE_LIMIT
                ]

            ref_speeds, port_speeds = speeds(ref), speeds(port)
            moving = [v for v in ref_speeds + port_speeds if v > 0]
            every_run_agrees = (
                bool(ref_speeds)
                and bool(port_speeds)
                and (all(v == 0 for v in ref_speeds) or all(v == 0 for v in port_speeds))
                and not (all(v == 0 for v in ref_speeds) and all(v == 0 for v in port_speeds))
            )
            numbers = (
                f"WLED {rm['motion']['speed_px_per_s']:.1f} px/s against the port's "
                f"{pm['motion']['speed_px_per_s']:.1f}"
            )
            if every_run_agrees and moving and max(moving) > 2.0:
                findings.append(f"one side moves and the other is still: {numbers}")
                score += 40
            else:
                notes.append(
                    f"one side's motion estimate is zero and the other's is not ({numbers}), "
                    "and the runs on the two sides do not agree about which, so it is not scored"
                )
        elif abs(ratio - 1.0) > speed_floor:
            findings.append(
                f"speed is {ratio:.2f}x WLED's "
                f"({rm['motion']['speed_px_per_s']:.1f} vs {pm['motion']['speed_px_per_s']:.1f} px/s), "
                + margin_text(abs(ratio - 1.0), speed_floor)
            )
            score += 30 * min(3.0, abs(ratio - 1.0))

    hue, hue_worst = pooled_hue_distance(ref, port)
    hue_samples = min(
        rm.get("hue_samples_per_frame", 1e9) or 0.0,
        pm.get("hue_samples_per_frame", 1e9) or 0.0,
    )
    hue_is_solid = hue_samples >= HUE_MIN_SAMPLES_PER_FRAME
    hue_floor = max(
        floors["hue_distance"], internal_hue_spread(ref), internal_hue_spread(port)
    )
    spread_text = (
        f" (closest of {len(ref.hue_runs)}x{len(port.hue_runs)} run pairings; "
        f"the widest is {hue_worst:.2f})"
        if len(ref.hue_runs) * len(port.hue_runs) > 1
        else ""
    )
    if hue > hue_floor:
        if hue_is_solid:
            findings.append(
                f"the colours are far apart, hue distance {hue:.2f}{spread_text}, "
                + margin_text(hue, hue_floor)
            )
            score += 40 * hue
        else:
            notes.append(
                f"hue distance {hue:.2f}, but the histogram was built from only "
                f"{hue_samples:.0f} lit pixels a frame, which is too few to mean anything"
            )

    # T6: the reference's own grey floor, which is not a render difference.
    ref_floor = rm.get("channel_floor") or [0, 0, 0]
    port_floor = pm.get("channel_floor") or [0, 0, 0]
    if min(ref_floor) >= WHITE_FLOOR_LIMIT and (max(ref_floor) - min(ref_floor)) <= 6 and max(port_floor) < 4:
        notes.append(
            f"WLED's frame has a uniform floor of about {min(ref_floor)} counts on all three "
            "channels and the port's has none. That is upstream's uninitialised white "
            "channel arriving through the live view's qadd8(w, r) map, not a brighter "
            "render; coverage and brightness below are both inflated by it"
        )

    # A difference smaller than the side's own run-to-run spread is not a
    # difference. With one capture a side the spread is 0 and nothing changes.
    def floor(key: str) -> float | None:
        fixed = floors[key]
        if fixed is None or not windows_match:
            return None
        return max(fixed, ref.spread.get(key, 0.0), port.spread.get(key, 0.0))

    coverage = abs(rm["fraction_lit"] - pm["fraction_lit"])
    coverage_floor = floor("fraction_lit")
    if coverage_floor is not None and coverage > coverage_floor:
        findings.append(
            f"coverage differs: {rm['fraction_lit']:.0%} of WLED's frame is lit "
            f"and {pm['fraction_lit']:.0%} of the port's, "
            + margin_text(coverage, coverage_floor)
        )
        score += 30 * coverage

    brightness = abs(rm["mean_brightness"] - pm["mean_brightness"])
    brightness_floor = floor("mean_brightness")
    if brightness_floor is not None and brightness > brightness_floor:
        findings.append(
            f"mean brightness differs by {brightness:.0f} of 255 "
            f"({rm['mean_brightness']:.0f} vs {pm['mean_brightness']:.0f}), "
            + margin_text(brightness, brightness_floor)
        )
        score += 20 * (brightness / 255.0)

    return {
        "name": ref.name,
        "score": round(score, 2),
        "findings": findings,
        "notes": notes,
        "audio": audio,
        "class": cls,
        "floors": {k: v for k, v in floors.items()},
        "hue_distance": round(hue, 3),
        "hue_distance_worst": round(hue_worst, 3),
        "window_seconds": {
            "reference": round(min(ref.durations), 1) if ref.durations else None,
            "port": round(min(port.durations), 1) if port.durations else None,
        },
        "speed_ratio": None if ratio in (None, float("inf")) else round(ratio, 3),
        "coverage_delta": round(coverage, 4),
        "brightness_delta": round(brightness, 2),
        "runs": {"reference": ref.runs, "port": port.runs},
        "run_spread": {"reference": ref.spread, "port": port.spread},
        "reference": rm,
        "port": pm,
    }


def settings_of(cap: Capture) -> dict | None:
    """The controls a capture was actually taken at, whichever side wrote it.

    The reference tool writes `applied_state_fields` at the top level; the
    snapshot harness writes `applied.forced_from_reference` when it was run
    with --match-reference and nothing at all when it was not; the engine
    capture writes `applied.fields`.
    """
    value = cap.meta.get("applied_state_fields")
    if isinstance(value, dict) and value:
        return value
    applied = cap.meta.get("applied")
    if isinstance(applied, dict):
        for key in ("forced_from_reference", "fields"):
            value = applied.get(key)
            if isinstance(value, dict) and value:
                return value
    return None


def gamma_of(cap: Capture) -> float | None:
    """The output gamma the capture was taken with, when it says."""
    applied = cap.meta.get("applied")
    if isinstance(applied, dict) and "gamma_correct" in applied:
        try:
            return float(applied["gamma_correct"])
        except (TypeError, ValueError):
            return None
    return None


# The controls that change what an effect draws. `fx` is left out: the two sides
# number their effects differently and the name is what joins them.
SETTING_KEYS = ("pal", "sx", "ix", "c1", "c2", "c3", "o1", "o2", "o3", "m12")


def settings_differ(a: Capture, b: Capture) -> list[str]:
    """Which controls two captures disagree on, empty when they agree."""
    sa, sb = settings_of(a), settings_of(b)
    if sa is None or sb is None:
        return ["unknown"]
    out = []
    for key in SETTING_KEYS:
        if key not in sa or key not in sb:
            continue
        # One side writes the checkmarks as booleans and the other as 0 and 1.
        left, right = sa[key], sb[key]
        if isinstance(left, bool) or isinstance(right, bool):
            left, right = bool(left), bool(right)
        if left != right:
            out.append(f"{key} {sa[key]} against {sb[key]}")
    return out


def attribute(ref: Capture, port: Capture, engine: Capture | None) -> str:
    """Engine or front end, when there is an engine capture to tell them apart.

    Round 1's version answered this for every effect and was wrong for most of
    them: the engine captures were taken at the effect's own metadata defaults
    while the reference was taken at whatever the device had, and on a virtual
    clock half the speed of the other two. Two captures at different settings
    cannot tell you which half of the stack a difference is in. So the first
    thing here is a check that the settings match, and the answer when they do
    not is that there is no answer.
    """
    if engine is None:
        return "unknown, no engine capture"
    mismatch = settings_differ(ref, engine)
    if mismatch:
        detail = "settings unknown" if mismatch == ["unknown"] else ", ".join(mismatch[:4])
        return (
            "attribution unavailable: the engine capture was taken at different "
            f"settings ({detail}). Recapture it with capture_engine.py --match-reference"
        )
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
                 engine_used: bool, capture_facts: list[str]) -> None:
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
        "The motion estimate is phase correlation at frame gaps of 1, 2, 4 and "
        "up, sub-pixel refined. It refuses to answer rather than guess: a gap "
        "whose displacement has wrapped past a third of the frame is thrown "
        "away, the shortest usable gaps have to agree on the velocity, and the "
        "correlation peak has to be clearly taller than the next peak anywhere "
        "else, which is what rules out a stripe field or a checkerboard where "
        "there is no single answer. When any of those fails the direction is "
        "\"none/unclear\". It still reports one of eight compass points, so a "
        "reading 45 degrees away can be the same motion landing either side of "
        "a boundary, and those are called out and scored low.",
        "",
    ]

    lines += [
        "Every flag below says how far outside the noise floor it is. The floors "
        "are the p95 of what two runs of the same WLED firmware, four hours "
        "apart, disagree about, per effect class: 24 counts of mean brightness "
        "for a non-particle effect and 8 for a particle one, 0.05 and 0.06 of "
        "coverage, 2.5 and 1.4 of mean frame change, and a hue distance of 0.78 "
        "and 0.82. An audio effect is not scored on brightness or frame change "
        "at all, because the device's own two runs of those differ by more than "
        "any port ever could. verification/round-2/NOISE.md has the table and "
        "how it was measured.",
        "",
    ]

    if capture_facts:
        lines += ["The settings both sides were captured at:", ""]
        lines += [f"* {fact}" for fact in capture_facts]
        lines.append("")

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
        for n in r.get("notes", []):
            lines.append(f"* Note: {n}")
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

    lines += [
        "## Effects with no reference available",
        "",
        "These are in the port and not on the device, so nothing here has been "
        "compared against anything and none of them appears above. They are not "
        "findings and they are not clean either: they are unverified. The nine "
        "that come from WLED-MM rather than from stock WLED can only be checked "
        "by reading the MM source or by capturing a WLED-MM build.",
        "",
    ]
    lines.append(f"No reference ({len(port_only)}): {', '.join(port_only) or 'none'}")
    lines.append("")
    lines += ["## Effects only on the device", ""]
    lines.append(
        "In the device's catalog and not in this port, or captured on the device "
        "and not on this side."
    )
    lines.append("")
    lines.append(f"Only on the device ({len(ref_only)}): {', '.join(ref_only) or 'none'}")
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
        if r["findings"] or r.get("notes"):
            items = "".join(f"<li>{f}</li>" for f in r["findings"])
            items += "".join(f"<li><em>note:</em> {n}</li>" for n in r.get("notes", []))
            html.append("<ul>" + items + "</ul>")
        if r.get("attribution"):
            html.append(f"<div class='att'>{r['attribution']}</div>")
        html.append(f"<img loading='lazy' src='images/{r['slug']}.png'>")
        html.append("</div>")
    html.append("</body></html>")
    (out / "index.html").write_text("\n".join(html), encoding="utf-8")


# ---------------------------------------------------------------------------


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--reference", required=True, action="append",
                    help="the WLED device capture folder; repeat it for a second pass of the "
                         "same device and the metrics become the median with a spread")
    ap.add_argument("--port", default=None, action="append",
                    help="the snapshot harness capture folder; repeatable, same effect")
    ap.add_argument("--engine", default=None, help="a plain engine simulator capture folder")
    ap.add_argument("--out", required=True, help="where the report goes")
    args = ap.parse_args()

    if not args.port and not args.engine:
        sys.exit("give at least one of --port and --engine to compare against")

    out = Path(args.out)
    (out / "images").mkdir(parents=True, exist_ok=True)

    log("loading captures")

    def runs_under(folder: str) -> list[Path]:
        """Every capture run under a path, so pooling is the default.

        A folder with its own `captures/` is one run. A folder whose children
        have `captures/` is a set of runs and all of them are used, which is
        what makes `--reference <parent>` do the right thing without anybody
        having to remember to repeat the flag. Round 2's whole re-ranking came
        from having a second run of the device, and the tool did nothing to
        ask for one.
        """
        root = Path(folder)
        if (root / "captures").is_dir():
            return [root]
        children = sorted(d for d in root.iterdir() if (d / "captures").is_dir()) if root.is_dir() else []
        if children:
            log(f"  {root.name}: pooling {len(children)} run(s) found underneath it")
        return children or [root]

    # The reference is 8 bit and already at live-view resolution, so it only
    # needs the colour-depth half of the normalisation.
    ref_runs, skipped_ref = [], []
    for folder in args.reference:
        for run in runs_under(folder):
            loaded, skipped = load_side(run, quantise=True, label="device")
            ref_runs.append(loaded)
            skipped_ref += skipped
    ref = merge_runs(ref_runs)
    log(f"  device: {len(ref)} usable over {len(ref_runs)} run(s), {len(skipped_ref)} not")
    metric_problems = check_metrics_agree(ref_runs[0], Path(args.reference[0]))

    port, skipped_port, port_runs = {}, [], []
    if args.port:
        port_runs = []
        for folder in args.port:
            for run in runs_under(folder):
                loaded, skipped = load_side(run, quantise=False, label="port")
                port_runs.append(loaded)
                skipped_port += skipped
        port = merge_runs(port_runs)
        log(f"  port: {len(port)} usable over {len(port_runs)} run(s), {len(skipped_port)} not")
    engine, skipped_engine = (
        load_side(Path(args.engine), quantise=True, label="engine") if args.engine else ({}, [])
    )
    if args.engine:
        log(f"  engine: {len(engine)} usable, {len(skipped_engine)} not")

    # Whichever side is the thing under test. With only --engine, the engine is.
    subject = port if args.port else engine
    subject_label = "port" if args.port else "engine"

    audio_names = audio_effect_names()
    particle_names = particle_effect_names()
    results = []
    for name in sorted(set(ref) & set(subject)):
        r = compare_one(ref[name], subject[name],
                        effect_class(name, audio_names, particle_names))
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

    # The output gamma each side was captured with decides whether the particle
    # findings are visible at all, and round 1's report did not record it.
    capture_facts = []
    for label, side in (("device", ref), (subject_label, subject)):
        if not side:
            continue
        example = next(iter(side.values()))
        gamma = gamma_of(example)
        capture_facts.append(
            f"{label}: {example.runs} capture run(s), output gamma "
            + ("not recorded" if gamma is None else f"{gamma:g}")
            + (
                ", controls matched to the reference"
                if settings_of(example) is not None
                else ", controls not recorded"
            )
        )
    widest = sorted(
        ((max(c.spread.get("mean_brightness", 0.0) for c in (ref[n["name"]], subject[n["name"]])), n["name"])
         for n in results),
        reverse=True,
    )[:5]
    if widest and widest[0][0] > 0:
        capture_facts.append(
            "widest run-to-run spread in mean brightness: "
            + ", ".join(f"{name} {value:.0f}" for value, name in widest if value > 0)
            + ". A difference smaller than a side's own spread is not scored"
        )
    matched = sum(1 for n in results if not settings_differ(ref[n["name"]], subject[n["name"]]))
    capture_facts.append(
        f"{matched} of {len(results)} effects were captured at the same controls on both sides"
    )
    single_sided = [
        label
        for label, count in (("device", len(ref_runs)), (subject_label, len(port_runs) if args.port else 1))
        if count < 2
    ]
    if single_sided:
        capture_facts.append(
            "**"
            + " and ".join(single_sided)
            + " has only one capture run, which is a sample and not a measurement.** "
            "Two runs of the same WLED firmware disagree by up to 170 counts of mean "
            "brightness and flip the frozen flag on 15 of 216 effects. Capture that "
            "side again into a second folder and pass both"
        )

    write_report(results, out, ref_only, port_only, skipped_ref,
                 skipped_port or skipped_engine, metric_problems, bool(args.engine), capture_facts)
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
