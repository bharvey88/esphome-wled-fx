# Round 1 findings

Adversarial review of `wled_fx` v0.3.0 against a real WLED 16.0.1 device (Apollo
M-1, ESP32-S3, 64x64 HUB75).

Scope worked, in the order the brief asks for:

1. all 64 effects the registry offers on a 2D layout, flagged or not;
2. the remaining flagged non-audio effects, re-ranked with a motion estimator
   that does not alias (see `TOOLING.md`);
3. audio effects checked only for gross failure (black, frozen, wrong axis).

Evidence lives outside the repository. Paths used below:

* reference: `esphome-wled-fx-ref\captures\<Name>_<id>\`
* port: `esphome-wled-fx-port\captures\<Name>_<id>\`
* engine: `esphome-wled-fx-engine\captures\<Name>_<id>\`
* side by side: `esphome-wled-fx-compare\images\<Slug>.png`
* four fresh device captures made during this review, kept in the session
  scratchpad as `devA` (Game Of Life alone), `devB` (Colored Bursts then Game Of
  Life), `devC` (Aurora, Color Clouds, Slow Transition at pinned settings) and
  `devD` (Blink then Aurora). Each was taken with
  `tools/reference/capture_wled.py --outdir <scratch> --only ... --set ... --colors "255,160,0;0,0,0;0,0,0"`
  from the `wled-ref` worktree, and each run restored and verified the device
  state on exit.

Two real defects came out of it, one root cause each, plus a documentation gap
and a coverage gap. The rest of the 111 flags in `REPORT.md` are measurement
problems or legitimate differences; those are accounted for in `VERDICTS.md` and
`TOOLING.md`.

---

## F1 (S2, engine) The 2D particle renderer runs with WLED's gamma pairing switched off, so every particle effect is dimmer and harder edged than WLED's

**Effects:** all 15 2D particle effects, plus the 1D particle effects that do not
saturate. Worst first, by measured port-to-reference mean brightness:
PS Blobs 0.43, PS Starburst 0.47, PS Ballpit 0.52, PS Impact 0.55,
PS Attractor 0.61, PS Vortex 0.63, PS Spray 1D 0.65, PS Waterfall 0.69,
PS Box 0.70, PS Springy 0.71, PS Fuzzy Noise 0.75, PS GEQ Nova 0.76,
PS Ghost Rider 0.81, PS Fire 0.83, PS GEQ 2D 0.84, PS Volcano 0.85.

**What WLED does versus what the port does.** WLED's particle renderer splits
gamma in two on purpose: it gamma-corrects the per-particle brightness before
that brightness is distributed over the sub-pixels, then applies the *inverse*
gamma to each sub-pixel weight, so that once `show()` puts the whole strip
through the gamma table the spatial falloff comes out linear and a dying
particle fades on a gamma-corrected curve. In the port `gammaCorrectCol` is a
hard `false` and `gamma8` / `gamma8inv` are identity, so neither half runs.
Nothing puts the sub-pixel weights back, and the front end's single gamma stage
is applied to a buffer that was never pre-compensated.

**Evidence.**

* Median ratio of port to reference mean brightness: 0.805 over the 24 captured
  particle effects, against 1.000 over the 159 non-particle effects with enough
  light to measure. 71 percent of particle effects fall below 0.9, against 23
  percent of the others.
* Non-particle effects agree with the reference to three significant figures and
  quantile for quantile, which rules out a capture-path bias: Hiphotic
  228.0 / 227.8, Noise2D 228.3 / 227.9, Rotozoomer 233.0 / 235.7. At the 0.5,
  0.7, 0.8, 0.9, 0.95 and 0.99 quantiles of pixel value the two sides are
  identical for all three (Hiphotic 230/230, 238/238, 246/246, 255/255).
* The same quantile test on particle effects puts the port between the reference
  and `gamma8(reference)`, which is the signature of one of the two gamma steps
  missing rather than both: PS Box q0.5 WLED 113, port 82, `gamma8(113)` = 26;
  PS Impact q0.5 WLED 115, port 57, `gamma8(115)` = 27; PS Vortex q0.5 WLED 97,
  port 57, `gamma8(97)` = 17.
* Visible in the side by side: `images/PS_Vortex.png` (WLED's spiral arms carry a
  soft halo, the port's are thin and hard edged), `images/PS_Fire.png` (WLED's
  flames reach about two thirds up the panel, the port's about half),
  `images/PS_Impact.png`, `images/PS_Waterfall.png`.
* Settings identical on both sides (`capture_port.py --match-reference`); each
  port capture's `forced_from_reference` block equals the reference's
  `applied_state_fields`.

**Suspected root cause.**

* `components/wled_fx/wf_color.h:601-604` makes `gamma8`, `gamma8inv`, `gamma32`
  and `gamma32inv` identity functions.
* `components/wled_fx/wf_particle.cpp:24-28` sets
  `constexpr bool gammaCorrectCol = false;`, which switches off
  `wf_particle.cpp:676-677` (`brightness = gamma8(brightness)`),
  `wf_particle.cpp:742-745` and `wf_particle.cpp:876-878`
  (`pxlbrightness[i] = gamma8inv(...)`), and the 1D equivalents at
  `wf_particle.cpp:1476-1477` and `wf_particle.cpp:1540-1543`.
* Upstream: `refs/WLED/wled00/FXparticleSystem.cpp:609`, `:679-681`, `:804-805`
  for 2D and `:1466`, `:1533` for 1D, with `gammaCorrectCol` defaulting to
  `true` at `refs/WLED/wled00/wled.h:412`.
* PORTING.md section 6 ("Gamma is off") treats this as harmless because "colour
  correction belongs to the ESPHome light layer". That reasoning holds for a
  single gamma stage applied to a finished frame. It does not hold here, because
  the particle renderer uses a matched pair of gamma and inverse gamma *inside*
  the frame, and dropping both halves is not the same as dropping one stage at
  the end. There is no front-end gamma value that makes particle and
  non-particle effects both match WLED: at `gamma_correct: 1.0` the particles
  are too bright and too soft relative to everything else, at 2.8 they are too
  dark and too hard.

**Engine or front end:** engine. The engine-only captures show the same deficit.

**Suggested fix.** Give the engine WLED's real gamma tables behind `gamma8`,
`gamma8inv`, `gamma32` and `gamma32inv` in `wf_color.h` (WLED builds them with
`NeoGammaWLEDMethod::calcGammaTable(2.8)`), and set `gammaCorrectCol = true` in
`wf_particle.cpp`. Then say plainly that the front end is the one gamma stage,
exactly as WLED's `show()` is: the display front end's `gamma_correct` should
default to 2.8 and the README line "applied on the way to the display only"
should say it is WLED's `show()` stage rather than optional polish. If a real
gamma table is unwanted, the minimum consistent alternative is to delete the
`gamma8()` on particle brightness as well as the `gamma8inv()` on the sub-pixel
weights, and record it, which leaves the port equal to "WLED with gamma
correction turned off" rather than to neither setting.

**How to verify.** Re-run
`powershell -File tools\wsl\wfx.ps1 snapshot --match-reference` over the 15 2D
particle effects with the capture still at `gamma_correct: 1.0`, then recompute
port-to-reference mean brightness. The median over those effects must move from
0.805 to 1.00 plus or minus 0.05 while the non-particle median stays at 1.00,
and `images/PS_Vortex.png` must show the soft halo on both rows.

---

## F2 (S2 for one effect, S3 for two more, engine) `strip.getMaxSegments()` became the literal `1`, so three effects get a quarter of upstream's scratch block

**Effects:** Fireworks Starburst (visible at 64x64), Fw Starburst audio (same
code; not comparable here because the device has a real microphone),
Fireworks 1D (not visible at 64x64, visible on a taller matrix).

**What WLED does versus what the port does.** Upstream sizes the star or spark
pool from a scratch budget that doubles twice when few segments are in use:

    unsigned segs = strip.getActiveSegmentsNum();
    if (segs <= (strip.getMaxSegments() /2)) maxData *= 2;
    if (segs <= (strip.getMaxSegments() /4)) maxData *= 2;

With one segment on an ESP32 both tests are true, so `maxData` is
`FAIR_DATA_PER_SEG * 4` = 8192 bytes. The transform table says to replace
`getMaxSegments()` with `1`, and the port did, turning the tests into
`1 <= (1 / 2)` and `1 <= (1 / 4)`. Integer division makes both right-hand sides
0, both tests are false, and `maxData` stays at 2048. For Fireworks Starburst
that is 34 stars instead of 136 on a 64x64 panel, because
`numStars = 1 + (seg_len >> 3)` is 513 and the cap is what decides.

**Evidence.**

* `images/Fireworks_Starburst.png`: WLED's rows are visibly denser at all eight
  sampled moments.
* Connected lit blobs per frame at t = 1, 2, 3, 4, 5, 5.8 s, both sides at the
  reference's 32x32 view, threshold value > 8: WLED 44, 39, 50, 28, 49, 36;
  port 10, 26, 33, 25, 36, 23. Mean 41 against 26.
* Lit pixel counts over the same window run 67 to 117 on WLED and 19 to 72 on
  the port.
* Settings identical (`--match-reference`): `pal=11`, `sx=128`, `ix=128`.

**Suspected root cause.**

* `components/wled_fx/wf_effects_1d_e.cpp:677` and `:679` (Fireworks Starburst).
* `components/wled_fx/wf_effects_mm.cpp:497` and `:499` (Fw Starburst audio).
* `components/wled_fx/wf_effects_1d2d.cpp:519` and `:521` (Fireworks 1D).
* Upstream `refs/WLED/wled00/FX.cpp:3739-3742` for the fireworks case and the
  identical block in `mode_starburst`. `FAIR_DATA_PER_SEG` is
  `MAX_SEGMENT_DATA / MAX_NUM_SEGMENTS` at `refs/WLED/wled00/FX.h:101`, and
  `MAX_NUM_SEGMENTS` is 32 on an ESP32 (`refs/WLED/wled00/FX.h:88`).
* The port's own `FAIR_DATA_PER_SEG` (`components/wled_fx/wf_segment.h:59`, 2048)
  is correct; only the doubling is lost.

**Engine or front end:** engine.

**Suggested fix.** In all three places, drop the dead comparison and compute what
upstream computes with one segment out of 32, that is
`maxData = FAIR_DATA_PER_SEG * 4`, with a comment saying where the factor comes
from. Add the same note to PORTING.md's transform table: `strip.getMaxSegments()`
is 32 for arithmetic even though there is one active segment, because the two
are different questions. Grep for `(1 / 2)` and `(1 / 4)` before closing this.

**How to verify.** Recapture Fireworks Starburst and count connected blobs at the
reference's 32x32 view. The mean must rise from 26 to roughly 40, and
`images/Fireworks_Starburst.png` must show comparable density on both rows. For
Fireworks 1D, check that `seg.data_size()` grows from 2040 to about 8180 bytes
at 64x64.

---

## F3 (S3, documentation) Four behaviour changes are missing from PORTING.md's deviation list, and one of them explains a top-five flag

None of these is a rendering defect. Each is a deliberate, defensible change that
a second porter or a future upstream merge would undo by accident, because
nothing records it.

1. **`CRGBW` zeroes its white channel; WLED leaves it uninitialised.**
   `components/wled_fx/wf_color.h:471-478` and `:485-497` add `w = 0;` after
   every `hsv2rgb_rainbow(..., raw, true)` call. Upstream's
   `refs/WLED/wled00/src/dependencies/fastled_slim/fastled_slim.cpp:100-104` has
   `//rgbdata[3] = 0; // white` commented out, so `CRGBW(CHSV32(...))` at
   `refs/WLED/wled00/colors.h:165` leaves `w` indeterminate. The port is correct
   and upstream is undefined behaviour, but this is precisely the kind of
   improvement the copy-the-body rule exists to prevent, so it has to be written
   down to survive.

   It also explains one of the biggest flags in `REPORT.md`. Color Clouds scores
   129 with "coverage differs: 87 percent of WLED's frame is lit and 56 percent
   of the port's" and "mean brightness differs by 56 of 255". The reference frame
   carries a uniform grey floor of about 60 counts per channel (sampled colours
   (65,60,57), (65,64,57), (74,60,57)) which `CHSV32(hue, 255, vol)` cannot
   produce at any hue. It is the uninitialised white channel, and WLED's live
   view maps RGBW to RGB with `buffer[pos++] = qadd8(w, r)`
   (`refs/WLED/wled00/ws.cpp:236-238`), so it lands on all three channels. A
   fresh device capture at pinned settings (`devC`,
   `--only "Color Clouds" --set pal=0,sx=24,ix=32,c1=48,c2=64,c3=12`) reproduces
   the grey exactly: mean brightness 101.5 against the port's 37.2, the whole
   difference being the constant floor. The port is right; the reference shows
   upstream UB through a live view that adds W to RGB.

2. **Fire 2012 clamps `ignition` to the segment length.**
   `components/wled_fx/wf_effects_1d_a.cpp` computes
   `raw_ignition = seg_len/10 > 3 ? seg_len/10 : 3` and then
   `ignition = raw_ignition < seg_len ? raw_ignition : seg_len`. Upstream
   (`refs/WLED/wled00/FX.cpp`, `mode_fire_2012`) has
   `const uint8_t ignition = max(3, SEGLEN/10);` with no clamp, which walks past
   the heat array on a 2-pixel segment. The clamp is right; record it.

3. **Multi Comet audio's `m12` default is renumbered from 7 to 4.** The port's
   metadata says `m12=4` where WLED-MM says `m12=7`
   (`refs/WLED-MM/wled00/FX.cpp`, `_data_FX_MODE_MULTI_COMET_AR`). MM numbers
   pinwheel 7 because it inserts Circle (5) and Block (6)
   (`refs/WLED-MM/wled00/FX.h:406-413`); the port uses stock WLED's numbering
   where pinwheel is 4 (`components/wled_fx/wf_segment.h:30-34`). The
   translation is correct, and it is the only metadata string out of 223 that
   differs from upstream other than the already-recorded Scrolling Text one, so
   it needs a line to stop anyone changing it back.

4. **Scrolling Text's metadata drops `rev=0,mi=0,rY=0,mY=0`.** Consistent with
   deviation 2 (no reverse or mirror), but deviation 4 mentions only fonts and
   time tokens.

**Engine or front end:** neither; documentation.

**Suggested fix.** Add all four to the deviation list in PORTING.md. For item 1,
also add a warning to the comparison tooling notes, because it will keep
producing false flags on any effect that builds a colour through `CRGBW(CHSV32)`
or `CRGBW(CHSV)`.

**How to verify.** `tools/check_effect_names.py` still passes and the deviation
list has four new entries. No code change to verify.

---

## F4 (S3, coverage) Four effects have no reference capture at all and are unverified

Snow Fall, GEQ 3D, Paintbrush and Fireworks audio exist only in WLED-MM. The
device runs stock WLED 16.0.1 and its catalog of 220 effect slots holds none of
these names, so they appear in neither `REPORT.md` nor this review. Snow Fall in
particular is a non-audio 2D effect the owner will see on the panel, and nothing
has checked it.

**Suggested fix.** Either capture them from a WLED-MM build, or review them
against the MM source by reading, and mark them in the gallery as unverified so
the coverage claim stays honest. Their metadata strings already match MM exactly
apart from the trailing moon glyph in the display name.

**How to verify.** The round 2 verdict table has a row for each of the four.

---

## Candidates that did not survive the evidence

Recorded so a later round does not spend time on them again.

* **PS Ballpit emission rate.** `REPORT.md` reports 6.25x and the side by side
  reads as WLED spawning twice as fast. Counting connected blobs shows the same
  rate on both sides: WLED 3, 4, 6, 8, 10, 10 and port 2, 2, 4, 6, 7, 9 at
  t = 1 to 5.8 s, that is 1.46 blobs per second each, offset by about a second
  of settle. The lit-pixel counts differ only because of F1.
* **Game Of Life colour.** `REPORT.md` reports hue distance 0.85, and the
  reference really is pastel cyan where the port is a saturated rainbow at the
  same `pal=11`. It does not reproduce. Two fresh device captures, one straight
  from the idle preset (`devA`) and one with the palette already settled by a
  preceding effect (`devB`), both render the saturated rainbow the port renders:
  sampled colours (228,0,125), (213,206,0), (35,0,248), (0,202,165). The port is
  right and the original reference frame is a one-off.
* **Aurora colour.** Reference pink, port green, hue distance 0.89, at the same
  `pal=50`. Cause reproduced: Aurora picks its wave colours once, at
  `call == 0`, and WLED cross-fades a palette change over its transition time,
  so the first frame of the new effect sees the old palette. Captured with the
  palette already applied before the effect changed (`devD`,
  `--only "Blink,Aurora" --set pal=50,sx=24`) the device renders green, matching
  the port: (15,62,17), (15,66,17), (15,70,17). Not a port defect.
* **Slow Transition being static in the port.** Same mechanism. The effect
  redraws only when its `changed` test fires, and that test compares the segment
  palette with the one it latched. WLED's palette keeps moving during the
  cross-fade so it fires for seconds; the port applies a palette instantly so it
  never fires. The body is otherwise a faithful copy, the only real difference
  being the dropped `cct` handling, which is deviation 2.
* **PS Box wall behaviour, Tartan animation rate, Waving Cell speed, Sweep and
  Wipe coverage, Scan Dual direction, Rain, Fireworks 1D spark spread, Funky
  Plank and GEQ bar gaps.** All phase, aliasing or audio; `VERDICTS.md` gives
  the measurement that settles each.
* **Every "motion is opposite" flag.** The estimator that produced them aliases
  by 180 degrees above about 20 px/s, and its sign is inverted besides. See
  `TOOLING.md`.
* **Effect bodies.** All 223 port bodies were mechanically re-transformed from
  `refs/WLED/wled00/FX.cpp` and `refs/WLED-MM/wled00/FX.cpp` and diffed against
  the port after whitespace, comment and cast normalisation. Every difference
  found was either a documented deviation, a `byte` to `uint8_t` rename, a
  `bitRead`/`bitSet` expansion, a `min`/`max` to ternary rewrite, the added
  `Segment &seg` argument, or one of the items in F2 and F3. The shared helpers
  were checked the same way: `Segment::blur`, `Segment::blur2d` and
  `fast_color_scaleAdd` are equivalent to upstream statement for statement.

---

## F5 (S3, engine) Peak-driven audio effects are effectively blank with no microphone, which is what the port promises they will not be

**Effects:** Puddlepeak (worst), Ripple Peak, Waterfall, and anything else that
gates on `audio.sample_peak`.

**What WLED does versus what the port does.** This one is measured against the
port's own contract rather than against the device, because the device has a
real microphone. PORTING.md section 7 says the simulated source exists so that
"every audio effect animates and renders in the simulator". Puddlepeak does not.
It draws only on `sample_peak == 1` (`components/wled_fx/wf_effects_audio_vol.cpp`,
`mode_puddles_base`, the `if (samplePeak == 1)` branch), and the simulation
raises the flag with `g_sim.sample_peak = hw_random8() > 250;`
(`components/wled_fx/wf_audio.cpp:81`), which is 5 values out of 256, about 2
percent of frames. Each hit paints a run of a few pixels on a 4096-pixel canvas
and is then faded out, so the aggregate is nothing.

**Evidence.** Port mean brightness 0.01 and mean frame change 0.00 over the
whole capture, against 2.46 and 0.58 on the device. Ripple Peak 0.39 against
1.28. Waterfall's mean frame change is 6.01 against 37.59. All three are the
`sample_peak` family; the volume-driven and FFT-driven audio effects are all
lively on the port.

Note that upstream's own `simulateSound()`
(`refs/WLED/wled00/util.cpp:557-640`) never assigns `samplePeak` at all, so it
stays 0 forever and these effects are completely dead there. The port's 2
percent is already an undocumented improvement on upstream; it is just not
enough to satisfy the promise in section 7.

**Suspected root cause.** `components/wled_fx/wf_audio.cpp:81`. A beat rate tied
to the simulation's own volume envelope, for example firing when `volume_smth`
crosses upward through a threshold, would give a few beats a second and match
what the simulation's other fields already imply.

There is a second, harmless inconsistency in the same function.
`wf_audio.cpp:84-85` set `g_sim.max_vol = 31;` and `g_sim.bin_num = 8;` on every
frame, which clobbers what an effect wrote. PORTING.md section 7 states the
opposite contract ("The real source deliberately never overwrites them, so
whatever an effect wrote reaches the next analysis block"), and upstream's
`simulateSound()` keeps them as untouched statics. It changes nothing today
because the simulated peak does not consult them, but the contract and the code
disagree.

**Engine or front end:** engine.

**Suggested fix.** Drive `sample_peak` from the simulated volume envelope rather
than from a 2 percent coin flip, and seed `max_vol` and `bin_num` once rather
than every frame (or state in section 7 that the simulation owns them).

**How to verify.** Capture Puddlepeak, Ripple Peak and Waterfall from the
simulator. Mean frame change must rise above 0.5 on all three, and Puddlepeak's
mean brightness must be of the same order as the device's 2.46 rather than 0.01.
