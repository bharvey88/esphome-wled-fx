"""Behavioural metrics for a captured animation, shared by both sides.

Effects use random numbers and wall-clock time, so two renders of the same
effect never agree pixel for pixel, on the same device let alone on two
different ones. What can be compared is behaviour: is it moving, which way, how
fast, how much of the frame is lit, what colours are in it, is it symmetric.
That is what these measure.

**This module is the one copy.** ``tools/reference/capture_wled.py`` imports
``estimate_motion()`` and ``compute_metrics()`` from here rather than carrying
its own, so a difference in a number is a difference in the animation and never
a difference in how it was measured. It used to be a hand-kept duplicate across
a branch boundary, which is how a sign error survived in both.
``tools/compare/compare.py`` also recomputes the reference side's metrics from
its own frames and says so loudly if the answer does not match the
``meta.json`` the reference wrote, which keeps the claim honest across a
recapture.

Everything below the two shared functions is this side's own: the normalisation
that has to happen before a 64x64 render and a 32x32 live view can be compared
at all.

``test_metrics.py`` beside this file is the unit test. Run it after any change
here.
"""

from __future__ import annotations

import math

import numpy as np

# ---------------------------------------------------------------------------
# Shared with tools/reference/capture_wled.py, which imports them from here.
# ---------------------------------------------------------------------------

# The eight compass points a direction is reported as, anticlockwise from right.
OCTANTS = ["right", "up-right", "up", "up-left", "left", "down-left", "down", "down-right"]

# Frame gaps the velocity is measured at, longest last. A real translation
# gives the same velocity at every gap; an alias does not. The long gaps are
# there for an effect that creeps along at a fraction of a pixel a frame, where
# adjacent frames carry almost no signal; the short ones are there because a
# long gap on a fast effect wraps around.
MOTION_GAPS = (1, 2, 4, 8, 16, 32)

# A gap whose median displacement is under this is not telling us much; it is
# kept only if no longer gap is usable.
MOTION_MIN_DISPLACEMENT = 0.5

# How much taller the correlation peak has to be than the next one somewhere
# else in the frame before the displacement it names is the only answer. A
# pattern that repeats every few pixels, a checkerboard or a stripe field at
# the resolution limit, produces several peaks of the same height and has no
# single displacement to report.
MOTION_MIN_UNIQUENESS = 1.25

# A displacement bigger than this fraction of the frame cannot be told from its
# wrapped-around opposite, so the pair that produced it is thrown away.
MOTION_UNAMBIGUOUS_FRACTION = 0.35

# Below this the picture is not moving in any useful sense.
MOTION_MIN_SPEED = 0.5


def _direction_of(vx: float, vy: float) -> str:
    """The compass point for a velocity in image coordinates, y downwards."""
    if math.hypot(vx, vy) < MOTION_MIN_SPEED:
        return "none/unclear"
    angle = math.degrees(math.atan2(-vy, vx)) % 360
    return OCTANTS[int(((angle + 22.5) % 360) // 45)]


def _parabolic(before: float, peak: float, after: float) -> float:
    """Where the true maximum sits between three samples, in samples.

    A parabola through the correlation peak and its two neighbours. Clamped to
    half a sample, because a fit that wants to move the peak further than that
    is telling us the peak is somewhere else entirely.
    """
    denominator = before - 2.0 * peak + after
    if denominator == 0.0:
        return 0.0
    offset = 0.5 * (before - after) / denominator
    return float(min(0.5, max(-0.5, offset)))


def _phase_shift(a: np.ndarray, b: np.ndarray) -> tuple[float, float, float, float] | None:
    """How far `b` is displaced from `a`, in pixels, with two quality numbers.

    Phase correlation. Two things here are easy to get backwards and round 1's
    estimator got both:

    * For ``R = F(a) * conj(F(b))`` the correlation peak lands at *minus* the
      displacement from a to b, so the peak index has to be negated. Taking it
      as the displacement reports every direction as its opposite.
    * The correlation is circular, so a peak at index ``p`` and one at
      ``p - N`` are the same peak. Only displacements well inside half the
      frame can be trusted, which is what the caller's gap schedule is for.
    """
    if not np.any(a) or not np.any(b):
        return None
    h, w = a.shape
    fa = np.fft.fft2(a)
    fb = np.fft.fft2(b)
    r = fa * np.conj(fb)
    mag = np.abs(r)
    mag[mag == 0] = 1e-8
    corr = np.fft.ifft2(r / mag).real
    py, px = np.unravel_index(int(np.argmax(corr)), corr.shape)
    peak = float(corr[py, px])
    sharpness = peak / (float(np.abs(corr).mean()) + 1e-8)
    # The next tallest peak anywhere else. On a repeating pattern it is as tall
    # as the first one and the displacement is not knowable; on a real scene it
    # is far below.
    masked = corr.copy()
    for oy in (-2, -1, 0, 1, 2):
        for ox in (-2, -1, 0, 1, 2):
            masked[(py + oy) % h, (px + ox) % w] = -np.inf
    runner_up = float(masked.max())
    uniqueness = peak / runner_up if runner_up > 0 else float("inf")
    # Sub-pixel refinement. Without it the smallest displacement this can see
    # is one whole pixel, which at 20 fps is 20 px/s, and every effect slower
    # than that reads as standing still.
    sub_y = _parabolic(corr[(py - 1) % h, px], peak, corr[(py + 1) % h, px])
    sub_x = _parabolic(corr[py, (px - 1) % w], peak, corr[py, (px + 1) % w])
    # Wrap into the signed range, then negate: this is the shift of b from a.
    dy = (py if py < h / 2 else py - h) + sub_y
    dx = (px if px < w / 2 else px - w) + sub_x
    return -float(dx), -float(dy), sharpness, uniqueness


def estimate_motion(frames: np.ndarray, timestamps: np.ndarray) -> dict:
    """Which way the picture is travelling and how fast, with a confidence.

    Short baselines and several of them. The velocity is measured between
    frames 1, 2 and 4 apart, each as the median over every such pair in the
    capture, and the three have to agree: a genuine translation moves twice as
    far in twice the time, while a periodic pattern that the correlation
    latched onto at the wrong offset does not. When they disagree the direction
    is reported as unknown rather than as a number, because an unknown is
    something a reader can skip and a wrong number is not.

    Speed is in pixels per second at the resolution the frames arrived in.
    """
    unknown = {
        "direction": "none/unclear",
        "speed_px_per_s": 0.0,
        "confidence": 0.0,
        "vx_px_per_s": 0.0,
        "vy_px_per_s": 0.0,
        "agreement": 0.0,
        "uniqueness": 0.0,
        "note": "speed measured in pixels/second at the RECEIVED live-view resolution",
    }
    frame_count = frames.shape[0]
    if frame_count < 3:
        return unknown
    height, width = frames.shape[1], frames.shape[2]
    gray = frames.astype(np.float32).mean(axis=3)
    window = np.outer(np.hanning(max(height, 2)), np.hanning(max(width, 2)))
    prepared = [(gray[i] - gray[i].mean()) * window for i in range(frame_count)]
    limit = MOTION_UNAMBIGUOUS_FRACTION * min(height, width)

    per_gap: dict[int, tuple[float, float]] = {}
    displacement_of: dict[int, float] = {}
    sharpness_of: dict[int, float] = {}
    uniqueness_of: dict[int, float] = {}
    for gap in MOTION_GAPS:
        if gap >= frame_count:
            break
        vxs, vys, dists, sharps, uniques = [], [], [], [], []
        # At most 32 pairs a gap: enough for a stable median, cheap on a long
        # capture.
        starts = range(0, frame_count - gap, max(1, (frame_count - gap) // 32))
        for i in starts:
            j = i + gap
            dt = float(timestamps[j]) - float(timestamps[i])
            if dt <= 0:
                continue
            shift = _phase_shift(prepared[i], prepared[j])
            if shift is None:
                continue
            dx, dy, sharpness, uniqueness = shift
            vxs.append(dx / dt)
            vys.append(dy / dt)
            dists.append(math.hypot(dx, dy))
            sharps.append(sharpness)
            uniques.append(uniqueness)
        if not vxs:
            continue
        median_distance = float(np.median(dists))
        if median_distance > limit:
            # Wrapped around: this gap and every longer one are ambiguous.
            break
        per_gap[gap] = (float(np.median(vxs)), float(np.median(vys)))
        displacement_of[gap] = median_distance
        sharpness_of[gap] = float(np.mean(sharps))
        uniqueness_of[gap] = float(np.median(uniques))

    if not per_gap:
        return unknown

    # Gaps that actually moved the picture. A gap that saw a tenth of a pixel
    # is mostly measuring the sub-pixel fit's own error, and averaging it in
    # with a gap that saw four pixels drags the answer towards zero and then
    # fails the agreement test for a reason that is not about the animation.
    # The three shortest of those, because a gap near the ambiguity limit is
    # the one most likely to have wrapped and the shortest usable baseline is
    # always the safest.
    informative = sorted(g for g in per_gap if displacement_of[g] >= MOTION_MIN_DISPLACEMENT)[:3]
    if not informative:
        informative = [max(per_gap)]
    vectors = np.array([per_gap[g] for g in informative], dtype=float)
    vx, vy = float(np.median(vectors[:, 0])), float(np.median(vectors[:, 1]))
    speed = math.hypot(vx, vy)
    sharpnesses = [sharpness_of[g] for g in informative]
    uniqueness = float(np.median([uniqueness_of[g] for g in informative]))

    # Agreement across the gaps: 1 when they are identical and 0 when the
    # spread is as big as the velocity itself. The +1 keeps a nearly still
    # picture, where the spread is a fraction of a pixel per second, from
    # scoring 0 for no good reason.
    spread = (
        max(math.hypot(per_gap[g][0] - vx, per_gap[g][1] - vy) for g in informative)
        if len(informative) > 1
        else 0.0
    )
    agreement = max(0.0, 1.0 - spread / (speed + 1.0))
    sharpness = float(np.mean(sharpnesses)) if sharpnesses else 0.0
    # The old estimator's confidence was the peak ratio alone, and the report
    # gated on "> 3". Keeping the same scale means the thresholds still mean
    # something, but a pattern whose gaps disagree can no longer be confident.
    confidence = sharpness * agreement

    direction = _direction_of(vx, vy)
    if len(informative) > 1 and agreement < 0.6:
        direction = "none/unclear"
        confidence = min(confidence, 1.0)
    if uniqueness < MOTION_MIN_UNIQUENESS:
        # Several equally good answers, so there is no answer. This is the
        # Waving Cell and Distortion Waves case: a pattern at the resolution
        # limit, where round 1 reported 16.3 px/s against 1.1 px/s and neither
        # number meant anything.
        direction = "none/unclear"
        confidence = 0.0

    return {
        "direction": direction,
        "speed_px_per_s": round(speed, 2),
        "confidence": round(confidence, 2),
        "vx_px_per_s": round(vx, 2),
        "vy_px_per_s": round(vy, 2),
        "agreement": round(agreement, 3),
        "uniqueness": round(uniqueness, 2),
        "gaps_measured": sorted(informative),
        "note": "speed measured in pixels/second at the RECEIVED live-view resolution",
    }


def compute_metrics(frames: np.ndarray, timestamps: np.ndarray) -> dict:
    F = frames.shape[0]
    if F == 0:
        return {"empty": True}

    V = frames.max(axis=-1).astype(np.float32)  # value channel per pixel
    mean_brightness = float(V.mean())
    max_brightness = float(V.max())
    lit_threshold = 8.0
    fraction_lit = float((V > lit_threshold).mean())
    all_black = bool(max_brightness <= 1.0)

    if F >= 2:
        diffs = np.abs(frames[1:].astype(np.int16) - frames[:-1].astype(np.int16)).mean(
            axis=(1, 2, 3)
        )
        mean_frame_change = float(diffs.mean())
        max_frame_change = float(diffs.max())
    else:
        mean_frame_change = 0.0
        max_frame_change = 0.0
    frozen = bool(F >= 2 and mean_frame_change < 0.5)

    r = frames[..., 0].astype(np.float32) / 255.0
    g = frames[..., 1].astype(np.float32) / 255.0
    b = frames[..., 2].astype(np.float32) / 255.0
    maxc = np.maximum(np.maximum(r, g), b)
    minc = np.minimum(np.minimum(r, g), b)
    diff = maxc - minc
    diff_safe = np.where(diff == 0, 1, diff)
    hue = np.zeros_like(maxc)
    mask_r = maxc == r
    mask_g = (maxc == g) & ~mask_r
    mask_b = (~mask_r) & (~mask_g)
    hue[mask_r] = (60 * ((g - b) / diff_safe) % 360)[mask_r]
    hue[mask_g] = (60 * ((b - r) / diff_safe) + 120)[mask_g]
    hue[mask_b] = (60 * ((r - g) / diff_safe) + 240)[mask_b]
    lit_mask = V > lit_threshold
    hue_lit = hue[lit_mask]
    if hue_lit.size > 0:
        hist, _ = np.histogram(hue_lit, bins=12, range=(0, 360))
        dominant_hue_hist = (hist / hist.sum()).tolist()
    else:
        dominant_hue_hist = [0.0] * 12
    # How many pixels the histogram was actually built from. A twelve bin
    # histogram over thirty samples swings from one run to the next, so the
    # comparison weights the hue distance by this rather than treating every
    # hue reading as equally solid.
    hue_samples = int(hue_lit.size)
    hue_samples_per_frame = hue_samples / float(F)

    # The lowest per-channel value anywhere in the capture. WLED's live view
    # maps RGBW to RGB with qadd8(w, r), and upstream leaves the white channel
    # of a CRGBW built from a CHSV uninitialised, so a reference capture can
    # carry a constant grey floor on all three channels that the panel itself
    # never shows. When this is well above zero and roughly equal on all three,
    # that is what it is. See PORTING.md deviation 28.
    channel_floor = [int(frames[..., c].min()) for c in range(3)]

    lr_scores, tb_scores = [], []
    for f in frames:
        fl = f.astype(np.float32)
        lr_scores.append(1.0 - float(np.abs(fl - fl[:, ::-1, :]).mean() / 255.0))
        tb_scores.append(1.0 - float(np.abs(fl - fl[::-1, :, :]).mean() / 255.0))

    motion = estimate_motion(frames, timestamps)

    return {
        "frame_count": F,
        "mean_brightness": round(mean_brightness, 3),
        "max_brightness": round(max_brightness, 3),
        "fraction_lit": round(fraction_lit, 4),
        "all_black": all_black,
        "mean_frame_change": round(mean_frame_change, 3),
        "max_frame_change": round(max_frame_change, 3),
        "frozen": frozen,
        "dominant_hue_histogram_12bin": [round(x, 4) for x in dominant_hue_hist],
        "hue_sample_pixels": hue_samples,
        "hue_samples_per_frame": round(hue_samples_per_frame, 1),
        "channel_floor": channel_floor,
        "left_right_symmetry": round(float(np.mean(lr_scores)), 4) if lr_scores else None,
        "top_bottom_symmetry": round(float(np.mean(tb_scores)), 4) if tb_scores else None,
        "motion": motion,
    }


# ---------------------------------------------------------------------------
# Normalisation. This side only: the two captures do not arrive in the same
# shape, and comparing them without this measures the difference between the
# two capture paths rather than between the two renders.
# ---------------------------------------------------------------------------

# What WLED's live view sends for a 64x64 matrix. See LIVEVIEW_NOTES in the
# reference capture tool.
REFERENCE_VIEW = (32, 32)


# ---------------------------------------------------------------------------
# Effects a six second window cannot see
# ---------------------------------------------------------------------------
#
# Some effects are paced by an interval longer than the capture window, so a
# six second capture is two or three samples of a Bernoulli trial and its mean
# brightness is counting noise. Round 2 measured this the hard way: PS Starburst
# was carried out of round 1 as "the first thing to attack" at half the device's
# brightness, and at thirty seconds at matched controls it is 1.17 with its lit
# pixels a frame agreeing to within 2 percent. The device's own two six second
# runs of it measure 8.2 and 2.8.
#
# Derived from the pacing expression in each body, not from a list of effects
# that looked odd:
#
#   Lightning        SEGENV.aux0 = hw_random8(255 - speed) * 100, which at the
#                    default speed is 0 to 12.6 s between strikes, mean 6.3 s
#   PS Starburst     10 + hw_random16(255 - speed) frames between explosions,
#                    mean 62 frames, so two to four in a six second window
#   Slow Transition  the speed slider is the whole cycle length
#   the wipes        one pass of the strip per cycle at the default speed
#
# Both capture tools consult this, so the two sides use the same window, and
# the comparison says so when they do not.
SLOW_EFFECTS = {
    "Lightning": 30.0,
    "PS Starburst": 30.0,
    "PS Galaxy": 30.0,
    "Slow Transition": 30.0,
    "Sweep": 30.0,
    "Sweep Random": 30.0,
    "Wipe": 30.0,
    "Wipe Random": 30.0,
    "Tri Wipe": 30.0,
    "Tartan": 30.0,
    "Halloween Eyes": 30.0,
    "Fill Noise": 30.0,
}


def capture_seconds(name: str, default: float) -> float:
    """The window this effect needs, never shorter than the one asked for."""
    return max(default, SLOW_EFFECTS.get(name, 0.0))


def subsample(frames: np.ndarray, width: int, height: int) -> np.ndarray:
    """Take every n-th pixel, the way WLED's live view does.

    ws.cpp sends a matrix bigger than MAX_LIVE_LEDS_WS by nearest-pixel
    subsampling: every n-th column, and n-1 rows out of every n skipped. It does
    not average and it does not anti-alias, so averaging here would make a thin
    stroke that WLED drops into a grey smear that WLED never sent, and every
    single-pixel effect would look brighter on this side than on that one.
    """
    h, w = frames.shape[1], frames.shape[2]
    if (w, h) == (width, height):
        return frames
    if w < width or h < height:
        raise ValueError(f"cannot subsample {w}x{h} up to {width}x{height}")
    if w % width == 0 and h % height == 0:
        return frames[:, :: h // height, :: w // width, :]
    # Not a whole multiple, which is every panel that is not a power of two
    # bigger than the live view. WLED itself only ever subsamples by a whole
    # number, because it picks n from a doubling ladder, so this case never
    # arises against a real device; it arises when somebody points this at a
    # 32x8. Nearest index keeps the property that matters, which is that a one
    # pixel stroke is either kept or dropped and never smeared into grey.
    rows = np.floor(np.arange(height) * (h / height)).astype(int)
    cols = np.floor(np.arange(width) * (w / width)).astype(int)
    return frames[:, rows][:, :, cols]


def quantise_565(frames: np.ndarray) -> np.ndarray:
    """Round to RGB565 and back, the way a snapshot display stores a frame.

    ESPHome's in-memory snapshot display is a DisplayBuffer, which holds 16 bits
    per pixel, and reads them back out spread over the full 0 to 255 range. The
    real WLED device sends 8 bits a channel. Putting the reference through the
    same grinder is the only way the brightness and hue numbers mean the same
    thing on both sides; the alternative, leaving it out, shows up as a constant
    small hue and brightness bias on every single effect and buries the real
    differences under it.
    """
    f = frames.astype(np.uint16)
    r = (f[..., 0] >> 3) * 255 // 0x1F
    g = (f[..., 1] >> 2) * 255 // 0x3F
    b = (f[..., 2] >> 3) * 255 // 0x1F
    return np.stack([r, g, b], axis=-1).astype(np.uint8)


def hue_distance(a: list[float] | None, b: list[float] | None) -> float:
    """How far apart two 12-bin hue histograms are, 0 to 1.

    Half the total variation distance, which for two distributions that each sum
    to one is 0 when they agree and 1 when they share no bin at all. Circular
    shifts are not treated as near misses on purpose: a red effect that came out
    orange is exactly the kind of difference worth reporting.
    """
    if not a or not b:
        return 0.0 if a == b else 1.0
    va, vb = np.asarray(a, dtype=float), np.asarray(b, dtype=float)
    if va.sum() == 0 and vb.sum() == 0:
        return 0.0
    if va.sum() == 0 or vb.sum() == 0:
        return 1.0
    return float(np.abs(va - vb).sum() / 2.0)
