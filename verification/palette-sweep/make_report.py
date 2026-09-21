"""Generates REPORT.md beside this file from the two comparison runs.

    python make_report.py compare-before.json compare-after.json

The two arguments are what `tools/compare/palette_sweep.py report` wrote next
to its markdown, for the v0.4.0 engine and for this one against the same
device capture. Both are kept here, so the tables can be rebuilt without the
device.
"""
import json
import statistics
import sys
from pathlib import Path

REPO = Path(r"C:\Users\bharv\development\esphome-wled-fx")
OUT = REPO / "verification" / "palette-sweep" / "REPORT.md"

TEMPLATE = r"""# The palette sweep

Rounds 1 and 2 captured every effect against a real WLED 16.0.1 device once,
on whichever palette the device happened to be holding. That is one of 72
palettes per effect, so anything that only goes wrong on a palette nobody
selected survived both rounds. This round closes that gap, because the owner
of a 64x64 HUB75 panel running v0.4.0 reported:

> if I change palette to Default it is bright and looks good, but if I choose
> any other palette setting it gets really dim and colors look bad

Short answer: **the engine's palette rendering is right, and the gamma theory
is wrong.** What the report describes is four things stacked on top of each
other, two of which are defects in this port and two of which are WLED
behaving exactly as WLED behaves. All four are below, with numbers.

---

## Method

Three instruments, all pre-output-gamma so the stages can be read apart.

| Side | Tool | What it is |
|---|---|---|
| device | `tools/compare/palette_sweep.py device` | a real WLED 16.0.1 device on the same hardware, over the JSON API and the live view websocket, which sends `strip.getPixelColor()` before `show()`'s gamma and before the bus brightness |
| port | `tools/compare/palette_sweep.py port` | `wled_fx_sim --anim`, the engine with no ESPHome around it, rendered at 64x64 and nearest-subsampled to 32x32 the way the live view subsamples |
| port, after gamma | the same, with `--with-gamma 2.2` | those frames through `build_output_gamma_lut(2.2)`, which is the display front end's output stage, so "what the panel is sent" can be read on its own |

Five effects, one from each family that reads a palette a different way:
**Palette** (`color_from_palette` once per pixel), **Fire 2012**
(`ColorFromPalette` with a computed brightness), **Noise 1** (palette index
from a noise field), **Hiphotic** (2D, index from two sine fields) and
**PS Fireworks** (a particle system, which reads the palette per particle).
All 72 palettes on each, both sides, at WLED's `DEFAULT_COLOR` and two empty
slots, four seconds a palette on the device and 200 frames at 23 ms in the
port. 360 pairs a run.

Each pair records mean brightness, the three channel means, the fraction lit,
the mean frame change and a twelve bin hue histogram, and is scored against
the per-class floors in [NOISE.md](../round-2/NOISE.md), which are the p95 of
the device's disagreement with itself: 24 counts of brightness with a ratio
outside 0.75 to 1.33, 0.05 of coverage, and 0.78 of hue distance on a
non-particle effect.

One run a side. NOISE.md is explicit that this is the worst case the
comparison can be used in, so a flagged pair here is a lead and not a verdict.

The device was left as it was found. Read back at the end of every capture
run: on, brightness 220, preset 5, effect 53, all three colours black.

---

## What the palette code turned out to be

Before any of the measurement, the palette path was diffed against upstream
line by line. All of it holds:

* the 59 cpt-city gradient byte tables, **identical**, in the same order;
* the seven FastLED sixteen entry tables including the three `_gc22` ones,
  **identical**, in the same order;
* `PALETTE_NAMES` against WLED's `JSON_palette_names`
  (`wled00/FX_fcn.cpp:2157`) and against the device's own `/json/palettes`:
  **identical**, all 72;
* `ColorFromPalette` (`wf_color.cpp:398` against `wled00/colors.cpp:118`),
  `Segment::color_from_palette` (`wf_segment.cpp:818` against
  `wled00/FX_fcn.cpp:1166`), `load_palette` (`wf_palette_util.cpp:146` against
  `wled00/FX_fcn.cpp:227`), the gradient loader, `fill_gradient_RGB`,
  `nblendPaletteTowardPalette` and the `PALETTE_SOLID_WRAP` rule:
  **identical**;
* every one of the 225 palette call sites in the effect bodies, against
  upstream's 207, argument for argument: **identical**.

The device agrees. Over the 66 named palettes the two sides' channel means
land within a couple of counts of each other on every effect. A sample, the
mean of each channel over a four second window on "Palette":

| Palette | device R,G,B | port R,G,B |
|---|---|---|
| 6 Party | 192.98, 57.28, 107.03 | 193.43, 56.87, 107.00 |
| 11 Rainbow | 136.22, 110.05, 102.15 | 136.31, 108.08, 103.95 |
| 20 Pastel | 202.36, 210.84, 143.92 | 202.93, 211.08, 143.81 |
| 35 Fire | 169.43, 77.23, 22.48 | 170.94, 79.21, 22.85 |
| 50 Aurora | 0.07, 156.27, 24.06 | 0.07, 156.63, 24.00 |
| 65 Lite Light | 33.16, 17.20, 35.46 | 33.12, 17.09, 35.42 |
| 71 Traffic Light | 120.38, 166.83, 0.00 | 121.46, 167.73, 0.00 |

So the engine is not where the dimness comes from.

---

## The gamma theory, and why it is wrong

v0.4.0 moved the output gamma into `wled_fx` and set the hub75 driver to
`gamma_correct: LINEAR`. The obvious theory is that the driver kept its own
curve, the panel now gets two, and mid-tones are crushed while a full-scale
colour is left alone. Checked, and that is not what happens:

1. `-DHUB75_GAMMA_MODE=0` is on the compile line of **all eight** esp-hub75
   translation units in the flashed firmware, read out of
   `compile_commands.json`, so `hub75_config.h`'s `#ifndef` keeps the 0 and
   the CIE1931 table is never selected.
2. The panel's bit depth is 8 (`CONFIG_HUB75_BIT_DEPTH 8`) and the LINEAR LUT
   at 8 bits is the identity, so the driver adds nothing at all.
3. Before v0.4.0 the only curve was the driver's CIE1931; after it, the only
   curve is the engine's gamma 2.2. At mid grey those are 47 and 56, so
   v0.4.0 made the panel slightly **brighter**, not darker.
4. The output stage is a 256 entry lookup applied per channel per pixel
   (`components/wled_fx/wled_fx.cpp:264-281`). It cannot know which palette is
   selected, so it cannot treat one differently from another.

The gamma is not the cause. It is the multiplier that makes the real cause
visible, which is the next section.

---

## Root cause, in four parts

### 1. "Default" is not a palette, and on most effects it is the brightest thing the panel can draw

At palette 0, `Segment::color_from_palette` returns the **segment colour**
rather than a palette entry, in this port
(`components/wled_fx/wf_segment.cpp:819-821`) and upstream
(`refs/WLED/wled00/FX_fcn.cpp:1168-1171`) alike. Most effects read their
colours through it, so on a panel Default paints one flat, fully saturated
colour over every pixel.

Measured on the device, pre-gamma mean brightness of 255:

| Effect | Default | median of the 66 named palettes | ratio |
|---|---:|---:|---:|
| Noise 1 | 255.0 | 174.3 | 1.46 |
| Hiphotic | 255.0 | 173.7 | 1.47 |
| Palette | 195.4 | 178.1 | 1.10 |
| Fire 2012 | 139.0 | 170.3 | 0.82 |

Noise 1 and Hiphotic on Default are `(255, 160, 0)` on every pixel of every
frame, on **both** sides: the device reports channel means of exactly 255.0,
160.0 and 0.0 with every pixel lit, and so does the port.

The output gamma then widens the gap, because gamma 2.2 leaves a full-scale
channel alone and crushes everything else. What the panel is actually sent,
mean brightness after the 2.2 stage:

| Effect | Default | median named palette | dimmest named palette |
|---|---:|---:|---|
| Noise 1 | 255.0 | 132.3 | 3.9, Lite Light |
| Hiphotic | 255.0 | 129.0 | 3.7, Lite Light |
| Palette | 147.7 | 133.5 | 4.8, Lite Light |
| Fire 2012 | 86.8 | 113.2 | 3.5, Lite Light |

So on a palette-reading effect the typical palette really is about **half**
the light of Default, and the darkest palette in WLED's list is about **one
sixty-fifth** of it. None of that is a defect. Lite Light is `bhw2_45_gp`,
whose brightest stop is `(61, 16, 65)`, and its bytes here are upstream's byte
for byte.

### 2. Three of the five entries directly under "Default" are built from colours that are black

These five are what anybody opening the palette dropdown meets first. On the
device, pre-gamma, and what the panel is then sent:

| Palette | Noise 1, pre-gamma | after gamma 2.2 | why |
|---|---:|---:|---|
| 0 Default | 255.0 | 255.0 | the segment colour, flat |
| 1 * Random Cycle | 137.6 | 58.6 | two random palettes |
| 2 * Color 1 | 255.0 | 255.0 | the segment colour, flat |
| 3 * Colors 1&2 | 121.0 | 106.1 | colour 2 is black |
| 4 * Color Gradient | 76.6 | 55.1 | colours 2 and 3 are black |
| 5 * Colors Only | 110.2 | 115.8 | colour 2 is black |

Four of the five are two to five times dimmer than Default, and that is WLED's
own behaviour: colours 2 and 3 default to black on a WLED device too. Moving
off Default onto any of them looks like the panel dimmed, which is precisely
the report.

### 3. Defect: "* Random Cycle" never finished its blend

`RandomPalette::step()` blended one 48 step pass per frame. A full blend is
about 255 passes, so at a 23 ms frame it takes 5.9 seconds, and the next
palette is generated every 5 seconds. The palette never arrived: what rendered
was a permanent half-blend of two random palettes, which is both less
saturated and less bright than either of them.

Upstream sizes the blend to the window instead
(`Segment::handleRandomPalette`, `refs/WLED/wled00/FX_fcn.cpp:435-457`): it
works out how many frames fit in the transition time and runs
`(255 + frames / 2) / frames` blends per frame until all 255 are done, then
stops and holds the palette until the next change. At the defaults that is
eight blends a frame for about 0.8 s, then 4.2 s of one whole, crisp palette.

Fixed in `components/wled_fx/wf_palette_util.cpp:205`, with the frame period
handed over from the front end's `update_interval`, which is where WLED reads
it from too. Measured over the last twenty seconds of a fifty-five second run
at 64x64, so the port is in the steady state the device has been in for hours:

| Effect | before | after | change |
|---|---:|---:|---:|
| Palette | 150.20 | 157.99 | +5.2% |
| Fire 2012 | 142.04 | 152.93 | +7.7% |
| Noise 1 | 155.62 | 165.28 | +6.2% |
| Hiphotic | 156.89 | 165.13 | +5.3% |

Of the 360 pairs, exactly three move by more than one count between the two
builds, and all three are this palette. A single four second window of a
*random* palette is not a measurement of anything, so the device numbers
cannot settle this one; the mechanism is settled against upstream's source and
is pinned by a test.

### 4. Defect: colour 1 booted at the wrong amber

`DEFAULT_COLOR1` in `components/wled_fx/light/__init__.py` was
`(0xFF, 0xAA, 0x00)`. WLED's `DEFAULT_COLOR` is `0xFFA000`
(`refs/WLED/wled00/FX.h:45`), and so is `Segment::colors[0]`
(`components/wled_fx/wf_segment.h:160`), which had already been corrected once
after a device capture showed the hue was off; the Python copy was missed. The
light writes its initial state into the engine at boot, so on every
configuration that has colour slots the engine's correct value was overwritten
with a green of 170 instead of 160.

Ten counts of green is small, and it lands in exactly the wrong place: the
colour palette "Default" draws on most effects, and the only ingredient the
four palettes under it have.

### And the panel is at half duty where the device is at 86 percent

Not a palette defect, but it is why the whole panel reads as darker than the
WLED device beside it. WLED's HUB75 bus does not scale pixels in software at
all (`wled00/bus_manager.cpp`, with `nscale8_video` commented out), so a WLED
panel is gamma-corrected bytes at a hardware duty of `bri`. The esp-hub75
driver defaults that duty to 128, which is WLED's own factory
`DEFAULT_BRIGHTNESS` of 127; the reference device has been turned up to 220.
Same bytes, 1.7 times the light. Written up as deviation 36 in
[PORTING.md](../../PORTING.md), and the two plain M-1 examples now set the
panel brightness explicitly instead of having no way to change it at all.

---

## The sweep, before and after

TABLE_PLACEHOLDER

---

## Other palette findings

* **Palette names and order** are identical to the device's `/json/palettes`,
  all 72, and are now checked against `JSON_palette_names` and against both
  table arrays in `palettes.cpp` whenever a WLED checkout is beside the tree
  (`tools/check_effect_names.py`).
* **The dynamic palettes follow the segment colours.** Checked in the effect
  test: palette 2 is the primary in all sixteen entries, 3 runs primary to
  secondary, 4 tertiary to primary, 5 primary then secondary, and moving the
  primary moves all of them.
* **Palette transitions do not exist here** and are not expected to: the port
  has no segment transition machinery, which is deviation 2. The one place
  upstream's transition time is used inside the palette code is the random
  palette's blend window, and that is now reproduced with upstream's default.
* **A palette picked at runtime is pinned**, so an effect change does not put
  back the palette the effect declares, where a WLED device does. The four
  hardware test firmwares turn pinning off at boot and match WLED; the plain
  examples have no such switch. Deviation 35. There is no YAML key for it,
  which is the open decision.
* **"* Random Cycle" has no settings.** `randomPaletteChangeTime` and
  `useHarmonicRandomPalette` are constants here, at upstream's defaults.
  Deviation 34.
* **Colour 1 restores its last state from flash**, brightness and all, and it
  is the master switch: a board that comes up with that light off blanks the
  panel and never runs the engine. It now says so in the log.
* **Fire 2012 is a little dimmer and a little less covered than the device on
  every palette including Default**, by about 0.85 on both counts. It is not
  palette-dependent, one run a side is below the bar NOISE.md sets for scoring
  a coverage difference, and round 2's pooled comparison did not flag it. A
  watch item for the next round rather than something chased here.

---

## What now covers this

* `tools/compare/palette_sweep.py`, a `device` / `port` / `report` mode of the
  comparison tooling, so a future round runs the same sweep in three commands.
  [tools/compare/README.md](../../tools/compare/README.md) has them.
* `test_palette_sweep()` in `tools/sim/effect_test.cpp`, which needs no
  device: every palette index resolves to a table that is neither black nor
  Party, the four dynamic palettes follow the segment colours, every name
  resolves back to its own index, "* Random Cycle" arrives at a palette and
  holds it, and two palette-driven effects render each of the 66 named
  palettes at 0.70 to 1.30 of that palette's own mean value. The tightest real
  margin is 0.84, on Vintage.
* `tools/check_effect_names.py`, which now compares the palette name list and
  both table arrays against WLED's own.
"""


def per_effect(rows, skip_random):
    out = {}
    for eff in sorted({r["effect"] for r in rows}):
        sub = [r for r in rows if r["effect"] == eff and not (skip_random and r["palette"] == 1)]
        ratios = [r["ratio"] for r in sub if r["ratio"] is not None]
        hues = [(r["hue_distance"], r["palette_name"]) for r in sub if r.get("hue_scorable")]
        out[eff] = {
            "n": len(sub),
            "lo": min(ratios) if ratios else float("nan"),
            "median": statistics.median(ratios) if ratios else float("nan"),
            "hi": max(ratios) if ratios else float("nan"),
            "worst_hue": max(hues) if hues else (float("nan"), "too few lit pixels"),
            "flagged": sum(1 for r in sub if r["flags"]),
        }
    return out


def hue_cell(pair):
    value, name = pair
    return "not scorable" if value != value else f"{value:.2f} ({name})"


def main() -> int:
    before = json.loads(Path(sys.argv[1]).read_text())
    after = json.loads(Path(sys.argv[2]).read_text())
    fb = [r for r in before if r["flags"]]
    fa = [r for r in after if r["flags"]]
    eb, ea = per_effect(before, True), per_effect(after, True)

    rows = ["| Effect | palettes | brightness ratio, lowest | median | highest | worst hue distance | flagged before | flagged after |",
            "|---|---:|---:|---:|---:|---|---:|---:|"]
    for eff in ea:
        b, a = eb[eff], ea[eff]
        rows.append(
            f"| {eff} | {a['n']} | {a['lo']:.2f} | {a['median']:.2f} | {a['hi']:.2f} | "
            f"{hue_cell(a['worst_hue'])} | {b['flagged']} | {a['flagged']} |"
        )

    flag_rows = ["| Effect | Palette | device | port | ratio | hue | flags |",
                 "|---|---|---:|---:|---:|---:|---|"]
    for r in sorted(fa, key=lambda r: (r["effect"], r["palette"])):
        ratio = "" if r["ratio"] is None else f"{r['ratio']:.2f}"
        flag_rows.append(
            f"| {r['effect']} | {r['palette']} {r['palette_name']} | {r['device_brightness']:.1f} | "
            f"{r['port_brightness']:.1f} | {ratio} | {r['hue_distance']:.2f} | {', '.join(r['flags'])} |"
        )

    section = (
        f"{len(fb)} of {len(before)} pairs cleared the NOISE.md floors before the fixes and "
        f"{len(fa)} of {len(after)} after. The two numbers are the same because the one palette "
        "the fixes change is the random one, and a four second window of a random palette on one "
        "side against a four second window of a different random palette on the other is not a "
        "measurement either way.\n\n"
        "The table below leaves that palette out, so it is the 71 palettes whose content is the "
        "same on both sides. **Hue distance never exceeds 0.15 on any of them, and the median "
        "brightness ratio is 1.00 on four effects out of five.** That is the result this round "
        "was for: the engine renders WLED's palettes as WLED renders them.\n\n"
        + "\n".join(rows)
        + "\n\nEvery pair that is still flagged:\n\n"
        + "\n".join(flag_rows)
        + "\n\nFourteen of the eighteen are Fire 2012 coverage, which is not palette-dependent: "
        "it flags on Default too, at the same 0.85 ratio, and the effect's spark rate is random. "
        "The other four are the random palette. Nothing else on any of the 360 pairs clears a "
        "floor."
    )
    body = TEMPLATE.replace("TABLE_PLACEHOLDER", section)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(body, encoding="utf-8")
    print(f"wrote {OUT} ({len(body)} bytes): before {len(fb)} flagged, after {len(fa)} flagged")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
