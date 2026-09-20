# Round 2 summary

## The noise floor first

The whole reference set was captured from the device a second time and compared
against the first. Two runs of the same firmware disagree by up to 170 counts of
mean brightness, 0.61 of coverage and a hue distance of 1.00; 15 of 216 effects
flip the `frozen` flag and 3 flip `all_black`. Three of the comparison's four
thresholds sit below the p95 of that. `NOISE.md` has the table and the
replacement floors.

## Counts

| Verdict | Count |
|---|---:|
| defect, per effect | 0 |
| defect-watch (clears the floor, cause not found) | 1 (PS Box) |
| artefact | 22 |
| expected | 38 |
| clean | 152 |
| unresolved | 1 (PS Sonic Stream) |
| effects with a row in `VERDICTS.md` | 214, plus 9 with no reference |

| Severity | Findings |
|---|---|
| S1 | 0 |
| S2 | 3 (R2-1 gamma at the output, R2-2 no `_default_palette`, R2-4 the platform branches) |
| S3 | 5 (R2-3 palette reset, R2-5 the missing random draw, R2-6 the collision clamp, R2-7 deviation honesty, R2-8 slider labels) |

None of the eight is a single-effect render bug. Three are in the control layer
or the output stage, which no capture in either round could see; two are
platform branches that only bite on hardware nobody has run this on; three are
documentation.

## The round 1 fixes, verdict by verdict

**(a) The gamma.** The engine half is right and verified: both tables are byte
for byte upstream's `calcGammaTable(2.2f)` output over 512 entries, all 18
upstream in-effect call sites are present, `gammaCorrectCol` is pinned true as
upstream defaults it, and 2.2 is WLED's value and not 2.8. The output half was
not done and is now worse than before. WLED applies gamma 2.2 to the finished
frame in `show()` (`FX_fcn.cpp:1723-1724`, gated on `gammaCorrectCol`, default
true) and nothing in the bus layer adds any. The port's display front end
defaults `gamma_correct` to 1.0 and the shipped M-1 examples do not set it, so
on a HUB75 panel the port drives a mid-grey at 128 where WLED drives 56, and
Matrix's spawn pixel at 215 where WLED drives 175, which is exactly the round
trip the effect's `gamma8inv(175)` exists to make. The picture is washed out
and low contrast beside the device, and the pre-compensation round 1 added is
the reason. On a WS2812 strip through the light front end ESPHome's default of
2.8 makes the same midtone 37 against WLED's 56, and because ESPHome gammas
after brightness rather than before, a strip at half brightness emits 5 where
WLED emits 28. The two front ends also disagree with each other about where the
master-brightness number goes. R2-1.

**(b) The data budget.** Right for the board it was checked on, and the device
agrees: `maxseg: 64`, 4096 bytes, 68 stars, and Fireworks Starburst measures
13.5 and 14.6 on the device against 11.9, 12.5 and 11.7 on the port. Every
allocation caller checks its return and falls back safely, on the effects and in
the particle system. But the ESP32-S2 branch keys on `CONFIG_IDF_TARGET_ESP32S2`,
which is not defined in an ESPHome build, the PSRAM branch is tested before it
where upstream tests it after, there is no ESP8266 branch at all, and PSRAM
detection now keys on the user having written `psram:` rather than on the board.
An S3 with PSRAM and no `psram:` block renders 136 stars against the device's
68. R2-4.

**(c) The 120 bpm beat.** Correct in every way the brief asked about: outside
the switch so it is the same in all four modes, measured at 2.0 peaks a second
at both 23 ms and 50 ms frames, and it disturbs neither the volume nor the FFT
simulation. The three peak effects look sensible. One thing is undocumented: the
line it replaced was a random draw, so the port consumes one fewer random per
frame than upstream and every audio effect is on a different sequence. The
repository already knows: the black allowance for Fw Starburst audio was widened
because of it. R2-5.

**(d) The seven new deviations.** Four are honest. Deviation 27 says all three
peak effects were blank when only Puddlepeak was, deviation 30 claims to be the
only metadata difference when six MM strings also drop a moon glyph, and
deviation 31 lists four dropped keys when there are five changes. Three
citations are off. R2-7.

## PS Starburst

Not a defect. The body, the emitter, the mover, both renderers, the sizing
functions and all three struct layouts are upstream's field for field, and
`MAXPARTICLES_1D` is 2600 on both sides. The 0.50 was the capture window: the
explosion interval is `10 + hw_random16(255 - speed)` frames, a mean of 62 at
the default, so a 6 s window catches two to four explosions and the brightness
is counting noise. The device's own two runs measure 8.2 and 2.8. At 30 s at
matched controls, with the particle size forced small and large, the ratio is
1.17, 1.32 and 1.05 and the lit pixels a frame agree to within 2 percent.

## Top findings

1. **R2-1** The engine pre-compensates for an output gamma neither front end
   applies by default, so on a HUB75 panel the port drives Matrix's green at 215
   where WLED drives 175 and a mid-grey at 128 where WLED drives 56. The display
   front end's `gamma_correct` default of 1.0 is the wrong default now that the
   tables are real.
2. **R2-2** There is no `_default_palette`. On WLED palette 0 resolves to the
   effect's declared palette; here it is always Party. Proved on the device:
   Fire 2012 at `pal=0` and at `pal=35` are the same picture, hue distance 0.00.
3. **R2-4** The S2 branch of the data budget is unreachable, the branches are in
   the wrong order, there is no ESP8266 branch, and an S3 with PSRAM but no
   `psram:` in its YAML gets twice the device's star count.
4. **R2-3** 112 of 216 effects reset the palette on selection where WLED leaves
   it alone. Every other control matches on every effect.
5. **R2-5** The simulated beat dropped a random draw and moved every audio
   effect onto a different random sequence.
6. **T9 and T10** The comparison's thresholds were never checked against the
   instrument, and the hue distance, which is twelve of the 25 surviving flags,
   is pooled for neither side.
7. **R2-8** The two standard slider labels are "Speed" and "Intensity" where
   WLED's UI shows "Effect speed" and "Effect intensity".

## What could not be settled

* **PS Box.** The port is a consistent 0.79 of the device over three runs
  against a device spread of 1.4 counts, which clears the floor. The tilt code
  and the `hw_random16()` seeding of `seg.aux0` are upstream's and no cause was
  found.
* **PS Sonic Stream**, for the second round. It needs music in the room.
* **Fw Starburst audio's budget.** It is sized from core WLED's table and gives
  68 stars where WLED-MM's own `FAIR_DATA_PER_SEG` would give 136. There is no
  device that runs it, so nothing can settle which is right.
* **The nine WLED-MM effects** are still verified by reading and by the
  simulator sweeps only.
* **Whether ESP8266 is a supported target.** Nothing blocks it, several effect
  bodies branch on it, and the segment budget has no branch for it.

## Device read-back

Left as found and verified at the end of the session by reading `/json/state`:
on, brightness 220, preset 5, effect 53, all three colours [0, 0, 0]. Every
capture run in this round restored and verified the state on exit, the last one
at the end of the palette probe.
