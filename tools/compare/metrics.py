"""Behavioural metrics for a captured animation, shared by both sides.

Effects use random numbers and wall-clock time, so two renders of the same
effect never agree pixel for pixel, on the same device let alone on two
different ones. What can be compared is behaviour: is it moving, which way, how
fast, how much of the frame is lit, what colours are in it, is it symmetric.
That is what these measure.

**The functions here are the reference capture tool's, unchanged.**
``tools/reference/capture_wled.py`` on the ``wled-ref`` branch captured the real
WLED device with ``estimate_motion()`` and ``compute_metrics()`` written exactly
as they appear below. They are copied rather than imported because the two tools
were written on separate branches; the arithmetic is identical on purpose, so a
difference in a number is a difference in the animation and never a difference
in how it was measured. ``tools/compare/compare.py`` recomputes the reference
side's metrics from its own frames with this module and says so loudly if the
answer does not match the ``meta.json`` the reference wrote, which is what keeps
that claim honest.

Everything below the two shared functions is this side's own: the normalisation
that has to happen before a 64x64 render and a 32x32 live view can be compared
at all.
"""

from __future__ import annotations

import math

import numpy as np

# ---------------------------------------------------------------------------
# From tools/reference/capture_wled.py, unchanged. Do not "improve" either of
# these without changing it there too and recapturing: a metric that means one
# thing on one side and something else on the other is worse than no metric.
# ---------------------------------------------------------------------------


def estimate_motion(frames: np.ndarray, timestamps: np.ndarray) -> dict:
    F = frames.shape[0]
    if F < 3:
        return {"direction": "none/unclear", "speed_px_per_s": 0.0, "confidence": 0.0}
    H, W = frames.shape[1], frames.shape[2]
    gray = frames.astype(np.float32).mean(axis=3)
    window = np.outer(np.hanning(max(H, 2)), np.hanning(max(W, 2)))
    step = max(1, F // 8)
    pairs = [(i, i + step) for i in range(0, F - step, step)] or [(0, F - 1)]

    dxs, dys, confs, dts = [], [], [], []
    for i, j in pairs:
        a = (gray[i] - gray[i].mean()) * window
        b = (gray[j] - gray[j].mean()) * window
        if not np.any(a) or not np.any(b):
            continue
        fa = np.fft.fft2(a)
        fb = np.fft.fft2(b)
        R = fa * np.conj(fb)
        mag = np.abs(R)
        mag[mag == 0] = 1e-8
        r = np.abs(np.fft.ifft2(R / mag))
        peak_idx = np.unravel_index(np.argmax(r), r.shape)
        conf = float(r[peak_idx] / (r.mean() + 1e-8))
        py, px = peak_idx
        dy = py if py < H / 2 else py - H
        dx = px if px < W / 2 else px - W
        dt = timestamps[j] - timestamps[i]
        if dt <= 0:
            continue
        dxs.append(dx)
        dys.append(dy)
        confs.append(conf)
        dts.append(dt)

    if not dxs:
        return {"direction": "none/unclear", "speed_px_per_s": 0.0, "confidence": 0.0}

    confs_arr = np.array(confs)
    good = confs_arr > 3.0
    if good.sum() < max(1, len(confs_arr) // 3):
        return {
            "direction": "none/unclear",
            "speed_px_per_s": 0.0,
            "confidence": round(float(confs_arr.mean()), 2),
        }

    dxs_g = np.array(dxs)[good]
    dys_g = np.array(dys)[good]
    dts_g = np.array(dts)[good]
    speeds = np.sqrt(dxs_g**2 + dys_g**2) / dts_g
    mean_dx, mean_dy = float(np.mean(dxs_g)), float(np.mean(dys_g))
    mag = math.hypot(mean_dx, mean_dy)
    if mag < 0.5:
        direction = "none/unclear"
    else:
        angle = math.degrees(math.atan2(-mean_dy, mean_dx)) % 360
        dirs = [
            "right",
            "up-right",
            "up",
            "up-left",
            "left",
            "down-left",
            "down",
            "down-right",
        ]
        direction = dirs[int(((angle + 22.5) % 360) // 45)]
    return {
        "direction": direction,
        "speed_px_per_s": round(float(np.mean(speeds)), 2),
        "confidence": round(float(confs_arr[good].mean()), 2),
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


def subsample(frames: np.ndarray, width: int, height: int) -> np.ndarray:
    """Take every n-th pixel, the way WLED's live view does.

    ws.cpp sends a matrix bigger than MAX_LIVE_LEDS_WS by nearest-pixel
    subsampling: every n-th column, and n-1 rows out of every n skipped. It does
    not average and it does not anti-alias, so averaging here would make a thin
    stroke that WLED drops into a grey smear that WLED never sent, and every
    single-pixel effect would look brighter on this side than on that one.
    """
    F, h, w = frames.shape[0], frames.shape[1], frames.shape[2]
    if (w, h) == (width, height):
        return frames
    if w % width or h % height:
        raise ValueError(f"cannot subsample {w}x{h} to {width}x{height} by a whole number")
    return frames[:, :: h // height, :: w // width, :]


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
