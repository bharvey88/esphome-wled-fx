#!/usr/bin/env python3
"""Unit tests for the comparison metrics, on patterns whose answer is known.

Round 1's direction estimator reported every direction as its opposite and
aliased above about 20 px/s, and roughly half of the 111 flags in that report
came out of it. Nothing caught that because nothing ever fed it a pattern whose
true velocity was known in advance. This does.

    python3 tools/compare/test_metrics.py

Exit status is the number of failures.
"""

from __future__ import annotations

from pathlib import Path
import sys

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

from metrics import (  # noqa: E402
    compute_metrics,
    estimate_motion,
    hue_distance,
    quantise_565,
    subsample,
)

FAILURES = 0


def check(ok: bool, what: str, detail: str = "") -> None:
    global FAILURES
    print(f"  {'ok  ' if ok else 'FAIL'} {what}{(' - ' + detail) if detail else ''}")
    if not ok:
        FAILURES += 1


def moving_pattern(vx: float, vy: float, *, frames: int = 40, size: int = 32, fps: float = 20.0,
                   seed: int = 3) -> tuple[np.ndarray, np.ndarray]:
    """A broadband texture translating at exactly (vx, vy) pixels per second.

    Low-pass filtered noise, shifted in the Fourier domain by a phase ramp, so
    the displacement of every frame is exact to well under a pixel and the
    ground truth needs no interpolation of my own. The texture has features a
    few pixels across and does not repeat inside the frame, which is what a
    real effect mostly looks like and is the case the estimator has to get
    right. The cases it has to refuse are separate tests.
    """
    rng = np.random.default_rng(seed)
    spectrum = np.fft.fft2(rng.standard_normal((size, size)))
    ky = np.fft.fftfreq(size)[:, None]
    kx = np.fft.fftfreq(size)[None, :]
    spectrum = spectrum * np.exp(-(kx**2 + ky**2) / (2 * 0.12**2))
    out = np.zeros((frames, size, size, 3), dtype=np.uint8)
    timestamps = np.arange(frames, dtype=np.float64) / fps
    for f in range(frames):
        ramp = np.exp(-2j * np.pi * (kx * vx * timestamps[f] + ky * vy * timestamps[f]))
        image = np.fft.ifft2(spectrum * ramp).real
        lo, hi = image.min(), image.max()
        scaled = ((image - lo) / (hi - lo + 1e-9) * 255.0).astype(np.uint8)
        for c in range(3):
            out[f, :, :, c] = scaled
    return out, timestamps


def test_directions() -> None:
    print("Direction, the four cardinals and a diagonal")
    # 8 px/s at 20 fps is 0.4 px a frame, which the old F//8 baseline turned
    # into a 3 px displacement and got right by luck of the sign being
    # symmetric. It still has to come out right.
    cases = [
        (8.0, 0.0, "right"),
        (-8.0, 0.0, "left"),
        (0.0, 8.0, "down"),
        (0.0, -8.0, "up"),
        (8.0, -8.0, "up-right"),
        (-8.0, 8.0, "down-left"),
    ]
    for vx, vy, expected in cases:
        frames, ts = moving_pattern(vx, vy)
        m = estimate_motion(frames, ts)
        check(
            m["direction"] == expected,
            f"({vx:+.0f}, {vy:+.0f}) px/s reads as {expected}",
            f"got {m['direction']}, v=({m['vx_px_per_s']}, {m['vy_px_per_s']}), conf {m['confidence']}",
        )


def test_speed() -> None:
    print("Speed, including past the old estimator's aliasing limit")
    # The shipped estimator correlated frames F//8 apart, about 0.8 s, which on
    # a 32 px frame wraps at 16 px and so could not tell 20 px/s from -20 px/s.
    # These three are all above that.
    for vx in (5.0, 20.0, 60.0, 120.0):
        frames, ts = moving_pattern(vx, 0.0)
        m = estimate_motion(frames, ts)
        got = m["vx_px_per_s"]
        ok = m["direction"] == "right" and abs(got - vx) <= max(1.0, 0.1 * vx)
        check(ok, f"{vx:.0f} px/s right is measured as itself", f"got {got} px/s, conf {m['confidence']}")


def test_still_and_noise() -> None:
    print("Things that are not translating")

    still = np.zeros((30, 32, 32, 3), dtype=np.uint8)
    still[:, 8:20, 8:20, :] = 200
    ts = np.arange(30, dtype=np.float64) / 20.0
    m = estimate_motion(still, ts)
    check(m["direction"] == "none/unclear", "a still picture has no direction", f"got {m['direction']}")
    check(m["speed_px_per_s"] < 0.5, "a still picture has no speed", f"got {m['speed_px_per_s']}")

    black = np.zeros((30, 32, 32, 3), dtype=np.uint8)
    m = estimate_motion(black, ts)
    check(m["direction"] == "none/unclear", "an all black capture has no direction")

    rng = np.random.default_rng(7)
    noise = rng.integers(0, 256, size=(30, 32, 32, 3), dtype=np.uint16).astype(np.uint8)
    m = estimate_motion(noise, ts)
    check(
        m["direction"] == "none/unclear" or m["confidence"] < 3.0,
        "white noise is not reported as confident motion",
        f"got {m['direction']} at confidence {m['confidence']}",
    )


def test_periodic_is_not_confident() -> None:
    """A pattern that repeats every few pixels is where phase correlation lies.

    A one pixel checkerboard has no unambiguous displacement at all: every
    shift of two pixels looks like no shift. The estimator has to say so rather
    than produce a number, which is what the Waving Cell and Distortion Waves
    rows of round 1 needed and did not get.
    """
    print("A pattern at the resolution limit")
    frames = np.zeros((40, 32, 32, 3), dtype=np.uint8)
    ys, xs = np.mgrid[0:32, 0:32]
    ts = np.arange(40, dtype=np.float64) / 20.0
    for f in range(40):
        board = ((xs + ys + f) % 2) * 255
        for c in range(3):
            frames[f, :, :, c] = board
    m = estimate_motion(frames, ts)
    check(
        m["direction"] == "none/unclear" or m["confidence"] < 3.0,
        "a one pixel checkerboard is not reported as confident motion",
        f"got {m['direction']} at {m['speed_px_per_s']} px/s, conf {m['confidence']}, agreement {m['agreement']}",
    )


def test_sign_against_a_hand_shift() -> None:
    """The bug in one line: a single bright column stepping one pixel right."""
    print("Sign, on a pattern with only one feature")
    frames = np.zeros((30, 32, 32, 3), dtype=np.uint8)
    ts = np.arange(30, dtype=np.float64) / 20.0
    for f in range(30):
        frames[f, :, (4 + f) % 32, :] = 255
    m = estimate_motion(frames, ts)
    check(m["direction"] == "right", "one column per frame to the right reads as right", f"got {m['direction']}")
    check(
        abs(m["vx_px_per_s"] - 20.0) < 2.0,
        "and at 20 px/s, which is one pixel per 50 ms frame",
        f"got {m['vx_px_per_s']}",
    )


def test_metric_basics() -> None:
    print("The rest of compute_metrics")
    frames, ts = moving_pattern(8.0, 0.0)
    m = compute_metrics(frames, ts)
    check(m["frame_count"] == frames.shape[0], "frame count is reported")
    check(not m["all_black"] and not m["frozen"], "a moving textured field is neither black nor frozen")
    check(m["hue_sample_pixels"] > 0, "the hue histogram records how many pixels it was built from")
    check(len(m["channel_floor"]) == 3, "the per-channel floor is reported")

    # The white channel floor the live view adds shows up as a floor on all
    # three channels, which is what the report needs in order to say so.
    floored = np.clip(frames.astype(np.int16) + 60, 0, 255).astype(np.uint8)
    fm = compute_metrics(floored, ts)
    check(min(fm["channel_floor"]) >= 60, "a uniform grey floor is visible in channel_floor",
          f"got {fm['channel_floor']}")

    check(abs(hue_distance([1.0] + [0.0] * 11, [1.0] + [0.0] * 11)) < 1e-9, "identical hue histograms are 0 apart")
    check(abs(hue_distance([1.0] + [0.0] * 11, [0.0] * 11 + [1.0]) - 1.0) < 1e-9, "disjoint hue histograms are 1 apart")


def test_subsample() -> None:
    print("Normalisation")
    frames = np.arange(2 * 64 * 64 * 3, dtype=np.uint8).reshape((2, 64, 64, 3))
    out = subsample(frames, 32, 32)
    check(out.shape == (2, 32, 32, 3), "64x64 subsamples to 32x32")
    check(bool((out[0, 0, 1] == frames[0, 0, 2]).all()), "by taking every second pixel, not by averaging")

    # Round 1 could not run this at all: it raised on anything that was not a
    # whole multiple.
    odd = np.zeros((2, 17, 31, 3), dtype=np.uint8)
    out = subsample(odd, 8, 8)
    check(out.shape == (2, 8, 8, 3), "a 31x17 panel subsamples without raising")

    same = subsample(frames, 64, 64)
    check(same.shape == frames.shape, "a frame already at the target size is left alone")

    q = quantise_565(np.full((1, 2, 2, 3), 255, dtype=np.uint8))
    check(int(q.max()) == 255, "RGB565 round trip keeps full white at full white")


def main() -> int:
    test_directions()
    test_speed()
    test_sign_against_a_hand_shift()
    test_still_and_noise()
    test_periodic_is_not_confident()
    test_metric_basics()
    test_subsample()
    print(f"\n{FAILURES} failure(s)")
    return FAILURES


if __name__ == "__main__":
    raise SystemExit(main())
