#!/usr/bin/env python3
"""Capture reference renders of every WLED effect from a real device.

Talks only to the WLED JSON API (/json/...) and the live view websocket
(ws://<host>/ws). Never touches device configuration, presets, playlists,
reboot or update endpoints. Restores the device to its original state
(on/bri/preset) when finished, on error, or on Ctrl-C.

Run with the dedicated venv, e.g.:
    C:\\Users\\bharv\\wfx-ref-venv\\Scripts\\python.exe tools\\reference\\capture_wled.py

See --help for options.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
import time
import traceback
from collections import Counter
from pathlib import Path

import numpy as np
import requests
import websocket
from PIL import Image, ImageDraw

DEFAULT_HOST = "10.10.10.225"
DEFAULT_OUTDIR = r"C:\Users\bharv\development\esphome-wled-fx-ref"

MIN_HTTP_INTERVAL = 0.25   # seconds between HTTP requests to the device
SETTLE_SECONDS = 1.5       # wait after selecting an effect before capturing
DEFAULT_CAPTURE_SECONDS = 6.0
INTER_EFFECT_PAUSE = 0.4   # extra pause between effects, be gentle
HTTP_TIMEOUT = 8
WS_CONNECT_TIMEOUT = 5
WS_RECV_TIMEOUT = 0.5

RESERVED_NAMES = {"RSVD", "-", ""}

LIVEVIEW_NOTES = {
    "source_resolution": [64, 64],
    "downsample_method": (
        "WLED's live view websocket (ws.cpp:sendLiveLedsWs) nearest-pixel "
        "subsamples the matrix when width*height exceeds MAX_LIVE_LEDS_WS "
        "(1024 on ESP32): it takes every n-th column and skips n-1 out of "
        "every n rows, it does NOT average/anti-alias. For this 64x64 "
        "(4096px) matrix n=2, so the header reports width=32, height=32. "
        "Fine detail (thin text strokes, single-pixel sparkles) can alias "
        "or vanish at this resolution; that is a property of WLED's own "
        "live view, not the capture tool."
    ),
    "buffer_padding": (
        "The server allocates a buffer sized for used/n pixels but the "
        "declared width*height in the 4-byte header is used/(n*n); only "
        "read width*height RGB triplets after the header and discard any "
        "trailing bytes in the websocket message (matches the official "
        "liveviewws2D.htm client behaviour)."
    ),
    "color_scaling": (
        "Live view sends strip.getPixelColor() values, i.e. the effect's "
        "own render-buffer output. WLED applies global/segment brightness "
        "scaling and gamma correction later, at the bus output stage, so "
        "live view frames are NOT brightness-scaled (bri=220 has no visual "
        "effect on captured pixel values) and are NOT gamma-corrected. "
        "A later agent comparing an ESPHome port's un-gamma-corrected, "
        "full-brightness render against these frames is comparing like "
        "for like; do not apply gamma or the device's bri=220 scale to "
        "either side before diffing."
    ),
    "channel_order": (
        "RGB triplets, in that order. White channel is saturating-added "
        "into R/G/B (qadd8) before sending; this device reports rgbw=false "
        "so the white channel is always 0 and has no effect."
    ),
    "off_state": "If the device master switch were off, all channels would be forced to 0 regardless of effect (not applicable here; device stays on throughout capture).",
}


def log(msg: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)


# ---------------------------------------------------------------------------
# HTTP helpers (paced, retried, gentle)
# ---------------------------------------------------------------------------

_last_http_call = [0.0]


def _pace() -> None:
    now = time.time()
    wait = MIN_HTTP_INTERVAL - (now - _last_http_call[0])
    if wait > 0:
        time.sleep(wait)
    _last_http_call[0] = time.time()


def http_get(host: str, path: str, retries: int = 3):
    url = f"http://{host}{path}"
    last_err = None
    for attempt in range(retries):
        _pace()
        try:
            r = requests.get(url, timeout=HTTP_TIMEOUT)
            r.raise_for_status()
            return r.json()
        except Exception as e:  # noqa: BLE001
            last_err = e
            time.sleep(0.5 * (2 ** attempt))
    raise RuntimeError(f"GET {path} failed after {retries} attempts: {last_err}")


def http_post(host: str, path: str, payload: dict, retries: int = 3):
    url = f"http://{host}{path}"
    last_err = None
    for attempt in range(retries):
        _pace()
        try:
            r = requests.post(url, json=payload, timeout=HTTP_TIMEOUT)
            r.raise_for_status()
            try:
                return r.json()
            except Exception:  # noqa: BLE001
                return None
        except Exception as e:  # noqa: BLE001
            last_err = e
            time.sleep(0.5 * (2 ** attempt))
    raise RuntimeError(f"POST {path} failed after {retries} attempts: {last_err}")


# ---------------------------------------------------------------------------
# Device state save/restore
# ---------------------------------------------------------------------------


def save_original_state(host: str, outdir: Path) -> dict:
    state = http_get(host, "/json/state")
    outdir.mkdir(parents=True, exist_ok=True)
    (outdir / "original_state.json").write_text(json.dumps(state, indent=2))
    log(f"Saved original device state (on={state.get('on')}, bri={state.get('bri')}, ps={state.get('ps')})")
    return state


def restore_device_state(host: str, outdir: Path, original_state: dict = None) -> None:
    """Restore to preset 5, brightness 220, on -- the known original state.

    Loading preset 5 also restores the segment's original colours (they are
    part of the preset), so no separate colour POST is needed here -- but we
    verify colours against the state captured at the very start of the run
    in case the preset itself changed.
    """
    try:
        log("Restoring device to original state (preset 5, bri 220, on)...")
        # The transition time goes back with it: every capture request sends
        # transition 0 so an effect that latches the palette at call == 0 does
        # not latch the previous one, and that is a state field like any other.
        transition = (original_state or {}).get("transition", 7)
        http_post(host, "/json/state", {"on": True, "bri": 220, "ps": 5, "transition": transition})
        time.sleep(1.0)
        readback = http_get(host, "/json/state")
        (outdir / "restored_state_readback.json").write_text(json.dumps(readback, indent=2))
        ok = readback.get("on") is True and readback.get("bri") == 220 and readback.get("ps") == 5
        msg = f"Restore verification: on={readback.get('on')} bri={readback.get('bri')} ps={readback.get('ps')}"
        if original_state is not None:
            orig_seg0 = (original_state.get("seg") or [{}])[0]
            new_seg0 = (readback.get("seg") or [{}])[0]
            orig_col = orig_seg0.get("col")
            new_col = new_seg0.get("col")
            colours_ok = orig_col == new_col
            orig_fx = orig_seg0.get("fx")
            new_fx = new_seg0.get("fx")
            fx_ok = orig_fx == new_fx
            ok = ok and colours_ok and fx_ok
            msg += f" fx={new_fx}(orig {orig_fx}) colours_match_original={colours_ok}"
        log(f"{msg} -> {'OK' if ok else 'MISMATCH, check restored_state_readback.json'}")
    except Exception as e:  # noqa: BLE001
        log(f"WARNING: restore failed: {e}")


# ---------------------------------------------------------------------------
# Effect list / metadata
# ---------------------------------------------------------------------------


def fetch_effect_catalog(host: str, outdir: Path) -> list[dict]:
    effects = http_get(host, "/json/effects")
    fxdata = http_get(host, "/json/fxdata")
    palettes = http_get(host, "/json/palettes")
    info = http_get(host, "/json/info")

    outdir.mkdir(parents=True, exist_ok=True)
    (outdir / "effects.json").write_text(json.dumps(effects, indent=2))
    (outdir / "fxdata.json").write_text(json.dumps(fxdata, indent=2))
    (outdir / "palettes.json").write_text(json.dumps(palettes, indent=2))
    (outdir / "info.json").write_text(json.dumps(info, indent=2))

    catalog = []
    for i, name in enumerate(effects):
        if name.strip() in RESERVED_NAMES or name.strip().startswith("RSVD"):
            continue
        meta = fxdata[i] if i < len(fxdata) else ""
        catalog.append({"id": i, "name": name, "fxdata": meta})
    log(f"Effect catalog: {len(effects)} slots, {len(catalog)} capturable (skipped {len(effects) - len(catalog)} reserved)")
    return catalog


def audioreactive_present(outdir: Path) -> bool:
    try:
        info = json.loads((outdir / "info.json").read_text())
        return "AudioReactive" in (info.get("u") or {})
    except Exception:  # noqa: BLE001
        return False


# ---------------------------------------------------------------------------
# Live view websocket capture
# ---------------------------------------------------------------------------


def parse_liveview_frame(data: bytes):
    if len(data) < 4 or data[0] != ord("L") or data[1] != 2:
        return None
    w, h = data[2], data[3]
    needed = 4 + w * h * 3
    if len(data) < needed or w == 0 or h == 0:
        return None
    arr = np.frombuffer(data[4:needed], dtype=np.uint8).reshape((h, w, 3)).copy()
    return w, h, arr


def capture_liveview(host: str, seconds: float):
    """One websocket connection at a time; returns (frames, timestamps, errors)."""
    frames_raw = []
    timestamps = []
    errors = []
    ws = None
    try:
        ws = websocket.create_connection(f"ws://{host}/ws", timeout=WS_CONNECT_TIMEOUT)
        ws.settimeout(WS_RECV_TIMEOUT)
        ws.send(json.dumps({"lv": True}))
        start = time.time()
        while time.time() - start < seconds:
            try:
                opcode, data = ws.recv_data()
            except websocket.WebSocketTimeoutException:
                continue
            except Exception as e:  # noqa: BLE001
                errors.append(f"recv error: {e}")
                break
            if opcode == websocket.ABNF.OPCODE_BINARY:
                parsed = parse_liveview_frame(data)
                if parsed is not None:
                    frames_raw.append(parsed)
                    timestamps.append(time.time() - start)
    except Exception as e:  # noqa: BLE001
        errors.append(f"connect error: {e}")
    finally:
        if ws is not None:
            try:
                ws.send(json.dumps({"lv": False}))
            except Exception:  # noqa: BLE001
                pass
            try:
                ws.close()
            except Exception:  # noqa: BLE001
                pass
    return frames_raw, timestamps, errors


def select_effect(host: str, effect_id: int, colors=None, extra_fields=None) -> None:
    """Apply an effect and its settings in one request, with no cross-fade.

    `transition: 0` matters more than it looks. WLED cross-fades a palette
    change over the segment's transition time, and an effect that samples the
    palette once, at `call == 0`, sees the *old* palette for the whole capture.
    Aurora is the clean case: it picks its wave colours at the first frame and
    a capture without this comes out in the previous effect's palette. It is
    sent per request rather than written to the config, so nothing about the
    device is changed.
    """
    seg_obj = {"id": 0, "fx": effect_id, "fxdef": True}
    if colors is not None:
        seg_obj["col"] = [list(c) for c in colors]
    if extra_fields:
        seg_obj.update(extra_fields)
    http_post(host, "/json/state", {"transition": 0, "seg": [seg_obj]})


def apply_colors(host: str, colors) -> None:
    """Set segment 0 colours directly (no effect change)."""
    http_post(host, "/json/state", {"transition": 0, "seg": [{"id": 0, "col": [list(c) for c in colors]}]})


def capture_one_effect(host: str, effect: dict, seconds: float, colors=None, extra_fields=None):
    """Returns (frames_raw, timestamps, applied_state, error_or_None)."""
    errors_all = []
    applied_state = None
    for attempt in range(2):
        try:
            select_effect(host, effect["id"], colors=colors, extra_fields=extra_fields)
        except Exception as e:  # noqa: BLE001
            errors_all.append(f"select failed: {e}")
            time.sleep(1.0)
            continue
        time.sleep(SETTLE_SECONDS)
        try:
            applied_state = http_get(host, "/json/state")
        except Exception as e:  # noqa: BLE001
            errors_all.append(f"state readback failed: {e}")
        frames_raw, timestamps, errors = capture_liveview(host, seconds)
        if errors:
            errors_all.extend(errors)
        if frames_raw:
            return frames_raw, timestamps, applied_state, ("; ".join(errors) if errors else None)
        log(f"  no frames on attempt {attempt + 1} for '{effect['name']}', retrying" if attempt == 0 else f"  still no frames for '{effect['name']}'")
        time.sleep(1.0)
    return [], [], applied_state, ("; ".join(errors_all) if errors_all else "no frames received after retry")


# ---------------------------------------------------------------------------
# Metrics
# ---------------------------------------------------------------------------

# There is one implementation of these and it lives in tools/compare. This tool
# used to carry its own copy, on the grounds that the two were written on
# separate branches, and the two copies were identical right down to a sign
# error in the direction estimator that nothing on either side could catch.
# Import it instead: a difference in a number is then a difference in the
# animation and never a difference in how it was measured.
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "compare"))

from metrics import capture_seconds, compute_metrics, estimate_motion  # noqa: E402,F401


# ---------------------------------------------------------------------------
# Output artifacts
# ---------------------------------------------------------------------------


def safe_folder_name(name: str, effect_id: int) -> str:
    slug = re.sub(r"[^A-Za-z0-9]+", "_", name).strip("_") or "unnamed"
    return f"{slug}_{effect_id:03d}"


def make_contact_sheet(frames: np.ndarray, timestamps: list, out_path: Path) -> None:
    F = frames.shape[0]
    if F == 0:
        img = Image.new("RGB", (400, 100), (40, 0, 0))
        d = ImageDraw.Draw(img)
        d.text((10, 40), "NO FRAMES CAPTURED", fill=(255, 80, 80))
        img.save(out_path)
        return
    n = min(8, F)
    idxs = sorted(set(int(round(x)) for x in np.linspace(0, F - 1, n)))
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
        x = pad + c * (tw + pad)
        y = pad + r * (th + pad)
        sheet.paste(tile, (x, y))
        ts = timestamps[idx] if idx < len(timestamps) else 0.0
        draw.text((x + 2, y + 2), f"{ts:0.1f}s", fill=(255, 255, 0))
    sheet.save(out_path)


def make_anim_webp(frames: np.ndarray, timestamps: list, out_path: Path) -> bool:
    if frames.shape[0] == 0:
        return False
    h, w = frames.shape[1], frames.shape[2]
    scale = max(1, 200 // max(h, w))
    imgs = [Image.fromarray(f, "RGB").resize((w * scale, h * scale), Image.NEAREST) for f in frames]
    if len(timestamps) > 1:
        avg_dt = float(np.mean(np.diff(timestamps)))
    else:
        avg_dt = 0.1
    duration_ms = max(20, int(avg_dt * 1000))
    try:
        imgs[0].save(
            out_path, save_all=True, append_images=imgs[1:], duration=duration_ms,
            loop=0, format="WEBP", quality=80, method=4,
        )
        return True
    except Exception as e:  # noqa: BLE001
        log(f"  WARNING: anim.webp write failed: {e}")
        return False


def is_already_captured(effect_dir: Path) -> bool:
    meta_path = effect_dir / "meta.json"
    if not meta_path.exists():
        return False
    try:
        meta = json.loads(meta_path.read_text())
        return meta.get("status") == "ok"
    except Exception:  # noqa: BLE001
        return False


def process_effect(host: str, effect: dict, seconds: float, captures_dir: Path, audio_present: bool,
                    colors=None, extra_fields=None) -> dict:
    name, eid = effect["name"], effect["id"]
    folder = safe_folder_name(name, eid)
    effect_dir = captures_dir / folder
    effect_dir.mkdir(parents=True, exist_ok=True)

    log(f"[{eid:3d}] {name}")
    frames_raw, timestamps, applied_state, error = capture_one_effect(
        host, effect, seconds, colors=colors, extra_fields=extra_fields)

    seg0_any = (applied_state or {}).get("seg", [{}])[0] if applied_state else {}
    meta = {
        "id": eid,
        "name": name,
        "folder": folder,
        "fxdata": effect["fxdata"],
        "audioreactive_usermod_present": audio_present,
        "requested_colors": [list(c) for c in colors] if colors is not None else None,
        "requested_extra_fields": extra_fields or None,
        "colours_readback": seg0_any.get("col"),
        "liveview_notes": LIVEVIEW_NOTES,
    }

    if not frames_raw:
        meta["status"] = "failed"
        meta["error"] = error or "no frames"
        meta["applied_state"] = applied_state
        (effect_dir / "meta.json").write_text(json.dumps(meta, indent=2))
        make_contact_sheet(np.zeros((0, 1, 1, 3), dtype=np.uint8), [], effect_dir / "sheet.png")
        log(f"  FAILED: {meta['error']}")
        return meta

    sizes = Counter((w, h) for w, h, _ in frames_raw)
    (mw, mh), _ = sizes.most_common(1)[0]
    kept = [(arr, ts) for (w, h, arr), ts in zip(frames_raw, timestamps) if w == mw and h == mh]
    frames = np.stack([a for a, _ in kept]).astype(np.uint8)
    ts_arr = np.array([t for _, t in kept], dtype=np.float64)
    dropped = len(frames_raw) - len(kept)

    metrics = compute_metrics(frames, ts_arr)
    measured_fps = round((len(ts_arr) - 1) / (ts_arr[-1] - ts_arr[0]), 2) if len(ts_arr) > 1 and ts_arr[-1] > ts_arr[0] else 0.0

    seg0 = (applied_state or {}).get("seg", [{}])[0] if applied_state else {}
    meta.update({
        "status": "ok" if not error else "ok_with_warning",
        "warning": error,
        "applied_state_fields": {
            "fx": seg0.get("fx"), "pal": seg0.get("pal"), "sx": seg0.get("sx"), "ix": seg0.get("ix"),
            "c1": seg0.get("c1"), "c2": seg0.get("c2"), "c3": seg0.get("c3"),
            "o1": seg0.get("o1"), "o2": seg0.get("o2"), "o3": seg0.get("o3"),
            "m12": seg0.get("m12"), "colours": seg0.get("col"),
        },
        "frame_count": int(frames.shape[0]),
        "dropped_mismatched_size_frames": dropped,
        "measured_fps": measured_fps,
        "received_size": {"width": int(mw), "height": int(mh)},
        "metrics": metrics,
    })

    np.savez_compressed(effect_dir / "frames.npz", frames=frames, timestamps=ts_arr)
    make_contact_sheet(frames, ts_arr.tolist(), effect_dir / "sheet.png")
    make_anim_webp(frames, ts_arr.tolist(), effect_dir / "anim.webp")
    (effect_dir / "meta.json").write_text(json.dumps(meta, indent=2))

    log(f"  ok: {frames.shape[0]} frames @ {mw}x{mh}, ~{measured_fps} fps, "
        f"mean_bri={metrics['mean_brightness']:.1f}, frozen={metrics['frozen']}, "
        f"all_black={metrics['all_black']}")
    return meta


# ---------------------------------------------------------------------------
# Index / summary
# ---------------------------------------------------------------------------

_AUDIO_NAME_KEYWORDS = [
    "freq", "gravcenter", "gravcentric", "gravfreq", "noisemeter", "geq",
    "sonic", "puddlepeak", "ripple peak", "matripix", "midnoise", "waverly",
    "blurz", "dj light", "gravimeter",
]


def is_audio_effect(entry: dict) -> bool:
    fxdata = entry.get("fxdata") or ""
    if re.search(r"(^|[,;])si=", fxdata):
        return True
    name = (entry.get("name") or "").lower()
    return any(k in name for k in _AUDIO_NAME_KEYWORDS)


_TRIGGER_NAME_KEYWORDS = ["halloween eyes", "lightning", "drip"]


def guess_black_reason(entry: dict) -> str:
    if is_audio_effect(entry):
        return ("audio-reactive default (fxdata declares a soundSim/'si=' default or the name implies "
                "audio input); the device has a real I2S microphone but the room was quiet during "
                "capture, so it likely never crossed the effect's trigger threshold")
    name = (entry.get("name") or "").lower()
    if any(k in name for k in _TRIGGER_NAME_KEYWORDS):
        return "effect triggers sparsely/randomly (by design); a several-second window may simply not catch a flash"
    return ("cause unclear from this pass; most likely the effect draws with the segment's secondary "
            "or tertiary colour (left at [0,0,0] this run) rather than the primary colour, or a "
            "palette index resolves to black under these settings -- recapture this effect with "
            "--only plus --set c2=...,c3=... (or a different --colors) to confirm")


def build_index(outdir: Path) -> None:
    captures_dir = outdir / "captures"
    entries = []
    if captures_dir.exists():
        for d in sorted(captures_dir.iterdir()):
            meta_path = d / "meta.json"
            if not meta_path.exists():
                continue
            try:
                meta = json.loads(meta_path.read_text())
            except Exception:  # noqa: BLE001
                continue
            entries.append(meta)
    entries.sort(key=lambda m: m.get("id", 0))

    (outdir / "index.json").write_text(json.dumps(entries, indent=2))

    failed = [e for e in entries if e.get("status") == "failed"]
    all_black = [e for e in entries if e.get("metrics", {}).get("all_black")]
    frozen = [e for e in entries if e.get("metrics", {}).get("frozen")]
    audio = [e for e in entries if e.get("status") != "failed" and is_audio_effect(e)]

    lines = ["# WLED reference capture summary", ""]
    lines.append(f"Captured {len(entries)} effect slots.")
    lines.append("")

    lines.append(f"## Failures ({len(failed)})")
    if failed:
        for e in failed:
            lines.append(f"- [{e['id']}] {e['name']}: {e.get('error', 'unknown error')}")
    else:
        lines.append("- none")
    lines.append("")

    lines.append(f"## Still all-black ({len(all_black)})")
    lines.append("Best-effort explanation per effect; verify with --only + --set before trusting it.")
    if all_black:
        for e in all_black:
            lines.append(f"- [{e['id']}] {e['name']}: {guess_black_reason(e)}")
    else:
        lines.append("- none")
    lines.append("")

    lines.append(f"## Frozen (no meaningful frame-to-frame change) ({len(frozen)})")
    if frozen:
        for e in frozen:
            tag = " (also all-black)" if e in all_black else ""
            lines.append(f"- [{e['id']}] {e['name']}{tag}")
    else:
        lines.append("- none")
    lines.append("")

    lines.append(f"## Audio-reactive effects ({len(audio)})")
    lines.append(
        "This device has a real I2S microphone (not simulated audio); AudioReactive usermod is on and "
        "reporting live room noise. These effects' behaviour depends on whatever the room sounded like "
        "during this capture run and is only loosely comparable to a port driven by simulated/fixed "
        "audio input. Recapture individually (with real sound present, or against the port's simulated "
        "input) before treating a mismatch here as a port bug."
    )
    if audio:
        for e in audio:
            m = e.get("metrics", {})
            state = []
            if m.get("all_black"):
                state.append("all-black")
            if m.get("frozen"):
                state.append("frozen")
            state_s = f" [{', '.join(state)}]" if state else " [animated]"
            lines.append(f"- [{e['id']}] {e['name']}{state_s}")
    else:
        lines.append("- none")
    lines.append("")
    (outdir / "SUMMARY.md").write_text("\n".join(lines))

    html = ["<!DOCTYPE html><html><head><meta charset='utf-8'><title>WLED effect reference captures</title>",
            "<style>",
            "body{font-family:sans-serif;background:#151515;color:#eee;margin:0;padding:20px;}",
            "h1{font-weight:normal;}",
            ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(260px,1fr));gap:14px;}",
            ".card{background:#222;border-radius:6px;padding:8px;}",
            ".card img{width:100%;border-radius:4px;display:block;background:#000;}",
            ".card .name{font-weight:bold;margin-top:6px;}",
            ".card .meta{font-size:12px;color:#aaa;}",
            ".failed{outline:2px solid #b33;}",
            ".black{outline:2px solid #666;}",
            ".frozen{outline:2px solid #b90;}",
            "</style></head><body>",
            f"<h1>WLED 16.0.1 effect reference captures ({len(entries)} effects)</h1>",
            "<div class='grid'>"]
    for e in entries:
        cls = []
        if e.get("status") == "failed":
            cls.append("failed")
        m = e.get("metrics", {})
        if m.get("all_black"):
            cls.append("black")
        if m.get("frozen"):
            cls.append("frozen")
        img_rel = f"captures/{e['folder']}/sheet.png"
        html.append(f"<div class='card {' '.join(cls)}'>")
        html.append(f"<img src='{img_rel}' loading='lazy'>")
        html.append(f"<div class='name'>[{e['id']}] {e['name']}</div>")
        if e.get("status") == "failed":
            html.append(f"<div class='meta'>FAILED: {e.get('error', '')}</div>")
        else:
            html.append(f"<div class='meta'>{e.get('frame_count', 0)} fr @ {e.get('measured_fps', 0)} fps, "
                         f"{e.get('received_size', {}).get('width')}x{e.get('received_size', {}).get('height')}, "
                         f"bri={m.get('mean_brightness')}, motion={m.get('motion', {}).get('direction')}</div>")
        html.append("</div>")
    html.append("</div></body></html>")
    (outdir / "index.html").write_text("\n".join(html))
    log(f"Index built: {len(entries)} entries ({len(failed)} failed, {len(all_black)} all-black, {len(frozen)} frozen)")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def parse_colors(s: str):
    """'R,G,B;R,G,B;R,G,B' -> [[r,g,b],[r,g,b],[r,g,b]]"""
    parts = s.split(";")
    if len(parts) != 3:
        raise ValueError("--colors needs exactly 3 semicolon-separated R,G,B triplets, e.g. 255,160,0;0,0,0;0,0,0")
    colors = []
    for p in parts:
        rgb = [int(x.strip()) for x in p.split(",")]
        if len(rgb) != 3 or any(v < 0 or v > 255 for v in rgb):
            raise ValueError(f"bad colour triplet: {p!r}")
        colors.append(rgb)
    return colors


_SET_BOOL_FIELDS = {"o1", "o2", "o3", "rev", "mi", "rY", "mY"}
_SET_INT_FIELDS = {"sx", "ix", "c1", "c2", "c3", "pal", "m12", "si"}


def parse_set(s: str) -> dict:
    """'sx=200,ix=100,pal=3' -> {"sx":200,"ix":100,"pal":3}, with correct types."""
    out = {}
    for pair in s.split(","):
        pair = pair.strip()
        if not pair:
            continue
        if "=" not in pair:
            raise ValueError(f"--set entries must be field=value, got {pair!r}")
        key, val = pair.split("=", 1)
        key = key.strip()
        val = val.strip()
        if key in _SET_BOOL_FIELDS:
            out[key] = val.lower() in ("1", "true", "yes", "on")
        elif key in _SET_INT_FIELDS:
            out[key] = int(val)
        else:
            raise ValueError(f"unsupported --set field {key!r} (supported: {sorted(_SET_BOOL_FIELDS | _SET_INT_FIELDS)})")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", default=DEFAULT_HOST)
    ap.add_argument("--outdir", default=DEFAULT_OUTDIR)
    ap.add_argument("--seconds", type=float, default=DEFAULT_CAPTURE_SECONDS)
    ap.add_argument("--only", default=None, help="Comma-separated effect names to (re)capture")
    ap.add_argument("--force", action="store_true", help="Recapture even if already captured")
    ap.add_argument("--index-only", action="store_true", help="Rebuild index.json/index.html/SUMMARY.md only, no device contact")
    ap.add_argument("--colors", default=None,
                     help="Segment colours to hold for the whole run, as 'R,G,B;R,G,B;R,G,B' "
                          "(primary;secondary;tertiary). Applied before the loop and re-asserted "
                          "with every effect selection so effect/palette defaults can't disturb it.")
    ap.add_argument("--set", default=None,
                     help="Extra segment fields to force on every selected effect, as "
                          "'field=value,field=value' (sx, ix, c1, c2, c3, o1, o2, o3, pal, m12, si, "
                          "rev, mi, rY, mY). Combine with --only to probe one disputed effect at "
                          "specific slider values.")
    args = ap.parse_args()

    outdir = Path(args.outdir)
    captures_dir = outdir / "captures"
    captures_dir.mkdir(parents=True, exist_ok=True)

    if args.index_only:
        build_index(outdir)
        return 0

    colors = parse_colors(args.colors) if args.colors else None
    extra_fields = parse_set(args.set) if args.set else None

    only_names = None
    if args.only:
        only_names = {n.strip().lower() for n in args.only.split(",") if n.strip()}

    original_state = save_original_state(args.host, outdir)
    try:
        catalog = fetch_effect_catalog(args.host, outdir)
        audio_present = audioreactive_present(outdir)

        if colors is not None:
            log(f"Applying hold colours before the loop: {colors}")
            apply_colors(args.host, colors)
            time.sleep(0.5)

        todo = catalog
        if only_names is not None:
            todo = [e for e in catalog if e["name"].strip().lower() in only_names]
            missing = only_names - {e["name"].strip().lower() for e in todo}
            if missing:
                log(f"WARNING: --only names not found in catalog: {sorted(missing)}")

        n_done = n_skipped = n_failed = 0
        for effect in todo:
            folder = safe_folder_name(effect["name"], effect["id"])
            effect_dir = captures_dir / folder
            force_this = args.force or (only_names is not None)
            if not force_this and is_already_captured(effect_dir):
                n_skipped += 1
                continue
            # A handful of effects are paced slower than the window; see
            # SLOW_EFFECTS in tools/compare/metrics.py.
            seconds = capture_seconds(effect.get("name", ""), args.seconds)
            meta = process_effect(args.host, effect, seconds, captures_dir, audio_present,
                                  colors=colors, extra_fields=extra_fields)
            if meta.get("status") == "failed":
                n_failed += 1
            else:
                n_done += 1
            time.sleep(INTER_EFFECT_PAUSE)

        log(f"Done. captured={n_done} skipped(existing)={n_skipped} failed={n_failed} of {len(todo)} requested "
            f"({len(catalog)} total capturable)")
        build_index(outdir)
        return 0
    except KeyboardInterrupt:
        log("Interrupted by user.")
        return 130
    except Exception:  # noqa: BLE001
        log("FATAL ERROR:")
        traceback.print_exc()
        return 1
    finally:
        restore_device_state(args.host, outdir, original_state)


if __name__ == "__main__":
    sys.exit(main())
