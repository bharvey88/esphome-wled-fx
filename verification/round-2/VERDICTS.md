# Round 2 verdicts

One row per effect examined. `defect` names the finding, `artefact` means the
difference is a measurement problem and says which, `expected` means a
legitimate difference and says why, `clean` means examined with evidence and
nothing worth acting on. `defect-watch` means it clears the noise floor and
the cause has not been found.

"device" is the two full reference runs, "port" the three full port runs, all
taken for this round at matched controls. "bri" is mean brightness out of 255
at the reference's 32x32 view, "hue" is the median port-against-device hue
distance with the device-against-device value beside it. The bar for a
difference being real is in `NOISE.md`.

## 1. The 25 effects the pooled comparison still flags

`esphome-wled-fx-compare2\REPORT.md`, 2 device runs against 3 port runs.
Round 1's tool on round 1's single runs flagged 111; this is 25.

| Effect | Verdict | Evidence |
|---|---|---|
| Strobe | artefact | the device's own two runs are 0.0 and 4.5 mean brightness and its `all_black` flag flips between them; the port's three are 2.1, 4.2 and 2.1, inside that range. Hue 1.00 is built from 0 lit pixels a frame |
| Tri Wipe | artefact | device 49.7 and 89.5, port 201.1, 243.7 and 42.4; the device's own spread is 40 counts and the port's is 201. At 30 s both sides settle: device 98.7, port 90.8, ratio 0.92. Hue 0.52 against a device-against-device 0.48 |
| Color Clouds | expected | deviation 28. The reference carries the uninitialised white channel as a grey floor of about 60 counts on all three channels through the live view's `qadd8(w, r)`; port 26.7 to 38.0 against device 93.1 and 85.0 is that floor. The device's own `frozen` flag flips between the two runs, so the animation half of the flag is noise too |
| Lightning | artefact | strike interval `hw_random8(255 - speed) * 100` is 0 to 12.6 s at the default speed, longer than the window. Device 1.4 and 0.8, port 0.1, 0.4 and 0.0; at 30 s device 0.55 and port 0.32. Bodies identical |
| Perlin Move | artefact | the device's own `frozen` flag flips between the two runs (frame change 0.48 then 0.83, either side of the 0.5 threshold). Device 21.6 and 31.4, port 22.9, 17.7 and 23.3 |
| Sinelon Rainbow | artefact | device 139.0 and 131.9, port 141.2, 125.1 and 129.0; frame change 3.34 and 3.38 against 3.36, 3.57 and 3.44; hue 0.20 against a noise floor of 0.17. The only flag is a direction word on a full-frame rainbow |
| Fill Noise | clean | device 158.1 and 155.5, port 156.9, 156.0 and 155.8; frame change 2.18 and 2.76 against 2.40, 2.46 and 2.14. At 30 s both are 255.0 and frozen. Same to three significant figures |
| PS Sparkler | artefact | both sides essentially dark and both `frozen` in the device's own two runs: device 0.3 and 0.3 at frame change 0.12 and 0.15, port 0.2, 0.2 and 0.3 at 0.10, 0.10 and 0.14 |
| Scan Dual | artefact | device 123.9 and 127.6, port 115.9, 124.4 and 114.6; frame change 1.91 and 2.09 against 1.75, 1.94 and 1.74; hue distance 0.00 on all three port runs |
| Sweep Random | artefact | hue 1.00 against a device-against-device hue of 1.00. One random colour against another |
| Wipe Random | artefact | hue 1.00 against a device-against-device hue of 1.00 |
| Random Colors | artefact | port runs give 0.91, 0.56 and 0.35 against device run 1; the two device runs are 0.75 apart |
| Game Of Life | artefact | 0.89, 0.83 and 0.85 against a device-against-device 0.87. Round 1's `devA`/`devB` work was answering a question the second full run answers for the whole class |
| Aurora | artefact | 0.88 on all three port runs against a device-against-device 0.88 |
| Theater Rainbow | artefact | 0.87, 1.00 and 1.00 against a device-against-device 0.91 |
| Blobs | artefact | 0.75, 0.58 and 0.76 against a device-against-device 0.88 |
| Blink Rainbow | artefact | port run 1 gives 0.75 and runs 2 and 3 give 0.04 and 0.24; the device's own two runs are 0.07 apart. Two of three port runs agree with the device, so the flag is the port's own run-to-run spread, which the report does not yet print for hue |
| Slow Transition | expected | hue 0.68 equals the device-against-device 0.68, and the port has no palette cross-fade so the effect's `changed` test never fires. At 30 s both sides are 255.0 and frozen |
| TV Simulator | artefact | 0.63, 1.00 and 1.00 against a device-against-device 1.00; a random sequence of simulated scenes |
| Plasma Ball | artefact | 0.58, 0.02 and 0.66 against a device-against-device 0.69 |
| Chase Random | artefact | 0.48, 0.71 and 0.22 against a device-against-device 0.48 |
| PS Attractor | artefact | 0.46, 0.35 and 0.42 against a device-against-device 0.97, and the device's own `frozen` flag flips between the two runs |
| Lissajous | artefact | 0.41, 0.23 and 0.04 against a device-against-device 0.52 |
| PS Impact | artefact | 0.38, 0.15 and 0.48 against a device-against-device 0.22 at about 100 lit pixels a frame; brightness overlaps (device 18.9 and 11.7, port 15.0, 23.2 and 18.0). Marginally over the hue floor on the median, under it on two of three runs |
| PS Ghost Rider | artefact | 0.36, 0.67 and 0.70 against a device-against-device 0.94; the effect defaults to pal=1, Random Cycle |

Twenty-two artefacts, two legitimate differences and one clean, no defects.
Every hue flag
in the list is at or below the same effect's hue distance between the two runs
of the device.

## 2. Effects examined individually outside the flagged list

| Effect | Verdict | Evidence |
|---|---|---|
| PS Starburst | clean | the round 1 residual. Device 8.2 and 2.8 over two 6 s runs, port 3.6, 5.5 and 5.7. At 30 s at matched controls, device 4.60 and port 5.40; at c1=0, 2.15 and 2.83; at c1=255, 9.60 and 10.08. Lit pixels a frame agree to within 2 percent at all three sizes. Bodies and struct layouts identical |
| PS Box | defect-watch | device 19.8 and 18.4, port 15.5, 15.2 and 14.5, a consistent 0.79 outside the device's own 1.4 count spread. No cause found; the tilt code and `seg.aux0` seeding are upstream's. Carried to round 3 |
| Fireworks Starburst | clean | 68 stars on both after `0d54ab6`; device 13.5 and 14.6, port 11.9, 12.5 and 11.7 (0.86), against a device spread of 1.1 and a port spread of 0.8. See R2-4 for the profile that would make it 136 |
| Matrix | clean | device 4.3 and 4.0, port 3.7, 3.7 and 4.4, hue 0.00. The spawn colour is (215, 255, 215) on both, which round 1 measured on the device. What the panel then shows is R2-1 |
| Pride 2015 | clean | the round 1 unresolved row. Device 124.8 and 92.0, port 169.8, 104.1 and 111.0, hue 0.12, 0.05 and 0.01. Both sides swing on `beatsin88_t(341, 96, 224)`, whose period is longer than the window; the ranges overlap |
| PS Vortex | clean | device 79.2 and 79.1, port 78.0, 78.2 and 78.2, frame change 51.55 and 52.46 against 52.09, 52.01 and 52.19, hue 0.00. The most reproducible effect in the set and the yardstick for the rest |
| PS Springy | clean | device 56.5 and 56.5, port 56.5, 56.5 and 56.3, hue 0.00 to 0.01 |
| PS Blobs | expected | deviation 20, `has_real_audio()`; the device takes the audio branch and the port the fallback |
| PS Spray | expected | same, `has_real_audio()` |
| Strobe Rainbow | expected | `blink(seg.color_wheel(seg.call & 0xFF), ...)`: the strobe colour is the frame counter, so the two sides differ by wherever their frame counters are after the settle. Bodies identical (`components/wled_fx/wf_effects_1d_b.cpp:61-63` against `refs/WLED/wled00/FX.cpp:251-253`); mean brightness 1.2 and 1.1 against 1.8, 3.2 and 1.6 |
| PS Sonic Stream | unresolved | still needs music in the room. Device 0.08 with a quiet room against the port's simulated spectrum; consistent with silence and with deviation 21, and proves neither |

## 3. Controls and defaults, all 216 effects

What `fxdef: true` left on the device, read back from `/json/state`, against
what the port loads from the same metadata string.

| Control | Effects where the two differ | Verdict |
|---|---:|---|
| `sx`, `ix`, `c1`, `c2`, `c3` | 0 of 216 | clean |
| `o1`, `o2`, `o3` | 0 of 216 | clean. Scanner Dual and Dynamic Smooth read back `o1=true` because both effect bodies set `check1` themselves on both sides |
| `m12` | 0 of 216 | clean |
| `pal` | 112 of 216 | defect R2-3, and R2-2 for what the value it resets to means |
| metadata string against the device's `fxdata` | 1 of 216 | expected, deviation 31, plus the seventh slider name R2-7 item 3 |
| control label text | all effects with a `!` on slider 0 or 1 | defect R2-8 |

## 4. A true 1D canvas, 60 and 300 pixels

Every registered effect run in the simulator at 60x1 and 300x1 for 300 frames
and checked for black, frozen, stuck at one end, ignoring `seg_len` and
lighting only the first pixels. 222 effects produced frames; 166 of them
declare that they run in 1D.

| Symptom | Effects | Verdict |
|---|---|---|
| black at both lengths | Halloween Eyes | expected: it idles for long random intervals, and 300 frames at 50 ms is 15 s |
| static by design at both lengths | Solid, Solid Pattern, Solid Pattern Tri, Percent, Spots, Fill Noise, Slow Transition, Sweep Random, Wipe Random, Tri Wipe, Scanner, Scanner Dual, Sparkle Dark, Twinkle, Freqmap, Blurz, Aurora, Color Clouds, Sunrise | expected: each is either a static pattern or has a period longer than 15 s. Mean frame change under 0.5 at both lengths, and the same on the device where a reference exists |
| animated at 60 and under the frozen threshold at 300 | Fireworks, Fireworks audio, Multi Comet, Oscillate, Puddlepeak, Rocktaves, Solid Glitter, Sparkle | expected: the per-pixel event rate is fixed while the canvas is five times larger, so the mean frame change falls by about the same factor. None is black or stuck |
| lit span does not grow with the strip | Chase Flash Rnd | expected: it walks one pixel per seven rendered frames at its own 20 to 30 ms delay, so 15 s reaches pixel 37 of 300. Upstream walks at the same rate |
| 2D-only effects on a 1D canvas | all 56 | clean: every one renders the solid fallback, as deviation 26 says. None animates and none is black |
| wrong direction relative to WLED's index order | none found | clean: the centre of mass moves the same way at both lengths for every effect that moves |

No 1D-only defect was found. The 1D-to-2D mapping modes are covered by the
main comparison, which runs every 1D effect on the 64x64 panel through the
`m12` the device applied, and all 167 of those rows are in section 5.

## 5. Every other effect compared against the device

Neither flagged by the comparison nor examined individually. Each row is the
device's two runs against the port's three; the verdict is `clean` when the
port's median brightness is inside the device's own range widened by that
range, and the hue distance is at or below the device's disagreement with
itself.

| Effect | device bri | port bri | hue port/device | hue device/device | Verdict | Note |
|---|---|---|---:|---:|---|---|
| Akemi | 52.1/46.5 | 54.4/55.4/55.9 | 0.05 | 0.05 | expected | audio, not scorable on brightness |
| Android | 62.0/63.2 | 72.8/72.4/71.7 | 0.00 | 0.00 | clean | ratio 1.16, inside the 0.75 to 1.33 band |
| Black Hole | 4.2/4.0 | 3.3/3.9/4.1 | 0.09 | 0.05 | clean | inside the device's own range |
| Blends | 221.0/221.0 | 221.3/221.0/221.3 | 0.01 | 0.01 | clean | inside the device's own range |
| Blink | 131.6/116.7 | 136.0/121.1/108.4 | 0.00 | 0.00 | clean | inside the device's own range |
| Blurz | 0.0/0.0 | 0.0/0.0/0.0 | 0.32 | 0.39 | expected | audio, not scorable on brightness |
| Bouncing Balls | 23.3/23.6 | 22.8/22.8/22.5 | 0.04 | 0.02 | clean | inside the device's own range |
| Bpm | 59.8/59.4 | 59.8/59.9/59.4 | 0.00 | 0.00 | clean | inside the device's own range |
| Breathe | 130.6/106.7 | 110.2/119.7/109.8 | 0.00 | 0.00 | clean | inside the device's own range |
| Candle | 157.3/140.5 | 145.6/153.6/158.1 | 0.00 | 0.00 | clean | inside the device's own range |
| Candle Multi | 151.5/151.5 | 152.0/151.6/152.2 | 0.00 | 0.00 | clean | inside the device's own range |
| Chase | 63.9/63.9 | 63.9/63.9/63.9 | 0.00 | 0.00 | clean | inside the device's own range |
| Chase 2 | 126.8/128.2 | 127.7/127.5/127.3 | 0.00 | 0.00 | clean | inside the device's own range |
| Chase 3 | 159.9/160.1 | 159.9/159.9/159.9 | 0.00 | 0.00 | clean | inside the device's own range |
| Chase Flash | 254.9/254.9 | 254.9/254.9/254.9 | 0.00 | 0.00 | clean | inside the device's own range |
| Chase Flash Rnd | 2.1/2.1 | 1.9/1.8/1.8 | 0.03 | 0.00 | clean | inside the device's own range |
| Chase Rainbow | 178.6/180.6 | 179.5/176.7/178.4 | 0.07 | 0.05 | clean | inside the device's own range |
| Chunchun | 32.0/31.3 | 33.6/32.7/33.6 | 0.05 | 0.02 | clean | inside the device's own range |
| Colored Bursts | 34.3/38.7 | 37.2/39.4/36.4 | 0.03 | 0.01 | clean | inside the device's own range |
| Colorful | 234.5/234.1 | 233.2/234.0/234.4 | 0.02 | 0.02 | clean | inside the device's own range |
| Colorloop | 194.6/195.9 | 195.5/195.1/195.6 | 0.10 | 0.13 | clean | inside the device's own range |
| Colortwinkles | 37.4/37.0 | 36.7/37.4/36.7 | 0.01 | 0.01 | clean | inside the device's own range |
| Colorwaves | 114.9/89.0 | 115.3/71.0/77.8 | 0.06 | 0.03 | clean | inside the device's own range |
| Crazy Bees | 3.8/3.3 | 3.2/3.6/3.5 | 0.29 | 0.41 | clean | inside the device's own range |
| DJ Light | 125.2/104.4 | 67.7/62.8/62.2 | 0.84 | 0.63 | expected | audio, not scorable on brightness |
| DNA | 6.3/6.8 | 6.5/6.7/6.5 | 0.03 | 0.01 | clean | inside the device's own range |
| DNA Spiral | 38.5/48.2 | 35.6/47.9/48.1 | 0.10 | 0.11 | clean | inside the device's own range |
| Dancing Shadows | 1.8/1.4 | 1.2/1.6/1.5 | 0.21 | 0.21 | clean | inside the device's own range |
| Dissolve | 116.5/114.8 | 115.7/115.5/114.7 | 0.00 | 0.00 | clean | inside the device's own range |
| Dissolve Rnd | 82.0/89.1 | 87.5/86.8/86.2 | 0.21 | 0.19 | clean | inside the device's own range |
| Distortion Waves | 206.6/208.1 | 207.3/207.2/208.2 | 0.02 | 0.02 | clean | inside the device's own range |
| Drift | 9.6/5.1 | 9.4/8.1/9.3 | 0.00 | 0.05 | clean | inside the device's own range |
| Drift Rose | 5.6/5.5 | 5.6/5.6/5.7 | 0.01 | 0.01 | clean | inside the device's own range |
| Drip | 8.7/8.7 | 8.5/8.8/8.6 | 0.00 | 0.00 | clean | inside the device's own range |
| Dynamic | 195.6/196.1 | 196.8/195.1/196.3 | 0.04 | 0.05 | clean | inside the device's own range |
| Dynamic Smooth | 218.4/217.1 | 217.0/217.1/214.9 | 0.03 | 0.03 | clean | inside the device's own range |
| Fade | 141.8/126.9 | 115.8/123.8/115.4 | 0.00 | 0.00 | clean | inside the device's own range |
| Fairy | 166.5/166.6 | 166.9/166.6/166.7 | 0.00 | 0.00 | clean | inside the device's own range |
| Fairytwinkle | 194.7/194.3 | 194.5/195.1/195.3 | 0.00 | 0.00 | clean | inside the device's own range |
| Fire 2012 | 136.8/139.1 | 134.1/132.6/134.4 | 0.00 | 0.00 | clean | ratio 0.97, inside the 0.75 to 1.33 band |
| Fire Flicker | 213.0/212.9 | 212.9/212.9/212.8 | 0.00 | 0.00 | clean | inside the device's own range |
| Firenoise | 119.6/119.6 | 119.6/119.6/119.6 | 0.01 | 0.01 | clean | inside the device's own range |
| Fireworks | 0.1/0.1 | 0.1/0.1/0.1 | 0.40 | 0.49 | clean | inside the device's own range |
| Fireworks 1D | 0.6/0.8 | 0.9/1.2/1.1 | 0.23 | 0.11 | clean | inside the device's own range |
| Flow | 169.5/169.6 | 169.7/169.6/169.6 | 0.00 | 0.00 | clean | inside the device's own range |
| Flow Stripe | 228.0/228.7 | 227.3/224.5/228.5 | 0.30 | 0.19 | clean | inside the device's own range |
| Freqmap | 0.4/1.2 | 0.3/0.3/0.3 | 0.04 | 0.01 | expected | audio, not scorable on brightness |
| Freqmatrix | 188.2/163.4 | 115.4/108.6/106.4 | 0.63 | 0.19 | expected | audio, not scorable on brightness |
| Freqpixels | 10.8/7.9 | 5.1/4.8/4.9 | 0.69 | 0.19 | expected | audio, not scorable on brightness |
| Freqwave | 196.4/161.8 | 124.5/103.4/105.2 | 0.57 | 0.07 | expected | audio, not scorable on brightness |
| Frizzles | 1.3/1.8 | 1.4/1.6/1.7 | 0.06 | 0.13 | clean | inside the device's own range |
| Funky Plank | 102.4/119.0 | 95.6/94.3/94.3 | 0.28 | 0.44 | expected | audio, not scorable on brightness |
| GEQ | 115.2/100.5 | 116.3/117.8/124.5 | 0.25 | 0.05 | expected | audio, not scorable on brightness |
| Ghost Rider | 4.5/4.4 | 4.2/3.6/4.4 | 0.03 | 0.03 | clean | inside the device's own range |
| Glitter | 226.3/226.3 | 226.3/226.3/226.3 | 0.00 | 0.00 | clean | inside the device's own range |
| Gradient | 225.0/225.0 | 225.0/225.0/225.0 | 0.00 | 0.00 | clean | inside the device's own range |
| Gravcenter | 91.9/32.1 | 42.6/52.6/63.9 | 0.04 | 0.04 | expected | audio, not scorable on brightness |
| Gravcentric | 107.5/80.0 | 63.0/98.9/73.7 | 0.04 | 0.07 | expected | audio, not scorable on brightness |
| Gravfreq | 218.6/88.8 | 111.7/86.4/96.6 | 0.10 | 0.28 | expected | audio, not scorable on brightness |
| Gravimeter | 203.4/31.8 | 64.4/84.6/101.4 | 0.08 | 0.16 | expected | audio, not scorable on brightness |
| Halloween Eyes | 0.0/0.0 | 0.0/0.0/0.0 | 0.00 | 0.00 | clean | inside the device's own range |
| Heartbeat | 113.0/113.8 | 114.5/114.4/114.6 | 0.01 | 0.01 | clean | inside the device's own range |
| Hiphotic | 228.0/227.9 | 227.7/228.0/227.6 | 0.01 | 0.01 | clean | inside the device's own range |
| ICU | 0.2/0.2 | 0.2/0.2/0.2 | 0.00 | 0.00 | clean | inside the device's own range |
| Juggle | 1.1/1.1 | 1.0/1.1/1.1 | 0.08 | 0.10 | clean | inside the device's own range |
| Juggles | 4.2/1.9 | 1.8/2.4/2.5 | 0.09 | 0.11 | expected | audio, not scorable on brightness |
| Julia | 138.2/138.9 | 138.4/137.6/138.0 | 0.00 | 0.00 | clean | inside the device's own range |
| Lake | 44.7/40.0 | 38.3/41.3/45.0 | 0.02 | 0.02 | clean | inside the device's own range |
| Lighthouse | 113.4/113.7 | 112.3/113.0/112.3 | 0.00 | 0.00 | clean | inside the device's own range |
| Loading | 225.0/225.0 | 225.0/225.0/225.0 | 0.00 | 0.00 | clean | inside the device's own range |
| Matripix | 103.6/44.8 | 25.9/24.5/22.3 | 0.25 | 0.22 | expected | audio, not scorable on brightness |
| Metaballs | 72.6/74.3 | 74.0/70.8/73.1 | 0.00 | 0.00 | clean | inside the device's own range |
| Meteor | 17.4/17.8 | 17.9/18.5/18.1 | 0.01 | 0.06 | clean | inside the device's own range |
| Midnoise | 144.9/125.3 | 145.6/157.2/169.7 | 0.12 | 0.07 | expected | audio, not scorable on brightness |
| Multi Comet | 2.9/2.8 | 2.8/2.8/2.8 | 0.00 | 0.00 | clean | inside the device's own range |
| Noise 1 | 230.0/229.9 | 230.3/229.9/230.2 | 0.02 | 0.00 | clean | inside the device's own range |
| Noise 2 | 126.9/126.8 | 126.9/126.9/126.9 | 0.01 | 0.01 | clean | inside the device's own range |
| Noise 3 | 73.2/71.9 | 71.4/71.4/71.4 | 0.01 | 0.01 | clean | inside the device's own range |
| Noise 4 | 121.3/122.5 | 120.6/121.2/121.1 | 0.00 | 0.01 | clean | inside the device's own range |
| Noise Pal | 196.8/197.3 | 197.4/197.4/197.8 | 0.00 | 0.00 | clean | inside the device's own range |
| Noise2D | 228.3/228.3 | 228.1/228.2/228.1 | 0.01 | 0.01 | clean | inside the device's own range |
| Noisefire | 122.1/123.1 | 79.9/99.8/103.8 | 0.05 | 0.04 | expected | audio, not scorable on brightness |
| Noisemeter | 197.8/101.6 | 101.5/135.6/147.8 | 0.05 | 0.05 | expected | audio, not scorable on brightness |
| Noisemove | 1.7/1.3 | 1.4/1.3/1.4 | 0.07 | 0.07 | expected | audio, not scorable on brightness |
| Octopus | 112.2/111.7 | 111.8/111.8/111.8 | 0.04 | 0.07 | clean | inside the device's own range |
| Oscillate | 24.6/24.6 | 24.6/24.7/24.6 | 0.00 | 0.00 | clean | inside the device's own range |
| PS 1D Balance | 46.0/45.8 | 45.9/45.8/46.1 | 0.00 | 0.00 | clean | inside the device's own range |
| PS Ballpit | 3.1/3.3 | 2.7/2.4/1.8 | 0.82 | 0.54 | clean | inside the device's own range |
| PS Chase | 53.8/53.8 | 53.8/53.8/53.9 | 0.00 | 0.00 | clean | inside the device's own range |
| PS Dancing Shadows | 6.8/7.1 | 6.4/5.4/6.2 | 0.17 | 0.09 | clean | inside the device's own range |
| PS DripDrop | 1.2/1.1 | 1.0/1.2/1.3 | 0.28 | 0.19 | clean | inside the device's own range |
| PS Fire | 112.0/111.1 | 111.5/111.0/110.8 | 0.00 | 0.00 | clean | inside the device's own range |
| PS Fire 1D | 8.2/8.0 | 8.0/7.9/8.1 | 0.02 | 0.03 | clean | inside the device's own range |
| PS Fireworks | 0.4/0.1 | 0.2/0.1/0.3 | 0.82 | 0.60 | clean | inside the device's own range |
| PS Fireworks 1D | 2.3/1.8 | 2.5/1.8/1.7 | 0.09 | 0.10 | clean | inside the device's own range |
| PS Fuzzy Noise | 118.0/112.4 | 110.6/114.8/113.8 | 0.06 | 0.04 | clean | inside the device's own range |
| PS GEQ 1D | 3.9/2.3 | 5.1/5.4/5.5 | 0.23 | 0.08 | expected | audio, not scorable on brightness |
| PS GEQ 2D | 10.1/1.5 | 12.6/13.4/13.6 | 0.35 | 0.12 | expected | audio, not scorable on brightness |
| PS GEQ Nova | 34.8/12.9 | 42.7/42.5/43.0 | 0.31 | 0.14 | expected | audio, not scorable on brightness |
| PS Galaxy | 1.7/1.9 | 1.8/1.8/1.7 | 1.00 | 1.00 | clean | inside the device's own range |
| PS Hourglass | 120.7/120.7 | 120.7/120.7/120.7 | 0.02 | 0.02 | clean | inside the device's own range |
| PS Pinball | 1.1/1.4 | 1.5/1.6/1.8 | 0.54 | 0.76 | clean | inside the device's own range |
| PS Sonic Boom | 0.0/0.0 | 9.8/9.9/10.1 | 1.00 | 1.00 | expected | audio, not scorable on brightness |
| PS Spray 1D | 2.2/2.2 | 2.8/2.4/2.3 | 0.02 | 0.05 | clean | inside the device's own range |
| PS Volcano | 6.1/6.0 | 5.7/5.7/5.8 | 0.03 | 0.04 | clean | inside the device's own range |
| PS Waterfall | 63.6/63.6 | 61.1/60.0/61.0 | 0.04 | 0.04 | clean | ratio 0.96, inside the 0.80 to 1.25 band |
| PacMan | 2.5/2.4 | 2.5/2.4/2.6 | 0.00 | 0.01 | clean | inside the device's own range |
| Pacifica | 154.6/156.9 | 154.5/158.7/151.1 | 0.01 | 0.00 | clean | inside the device's own range |
| Palette | 226.2/226.1 | 226.4/226.5/226.3 | 0.02 | 0.03 | clean | inside the device's own range |
| Percent | 161.5/161.5 | 161.5/161.5/161.5 | 0.00 | 0.00 | clean | inside the device's own range |
| Phased | 25.3/25.3 | 25.3/25.3/25.3 | 0.00 | 0.00 | clean | inside the device's own range |
| Phased Noise | 25.3/25.2 | 25.3/25.2/25.2 | 0.00 | 0.00 | clean | inside the device's own range |
| Pixels | 52.6/12.6 | 25.1/24.7/29.2 | 0.35 | 0.87 | expected | audio, not scorable on brightness |
| Pixelwave | 124.1/32.0 | 84.5/88.4/97.9 | 0.16 | 0.19 | expected | audio, not scorable on brightness |
| Plasma | 94.4/81.2 | 89.7/82.1/75.3 | 0.06 | 0.05 | clean | inside the device's own range |
| Plasmoid | 240.0/212.3 | 130.4/187.3/190.6 | 0.11 | 0.22 | expected | audio, not scorable on brightness |
| Polar Lights | 31.5/31.6 | 31.6/31.6/31.5 | 0.00 | 0.00 | clean | inside the device's own range |
| Popcorn | 1.3/1.6 | 1.4/1.5/1.6 | 0.29 | 0.29 | clean | inside the device's own range |
| Puddlepeak | 2.5/1.6 | 0.1/0.5/0.5 | 0.41 | 0.44 | expected | audio, not scorable on brightness |
| Puddles | 5.2/2.0 | 3.8/4.5/4.7 | 0.12 | 0.14 | expected | audio, not scorable on brightness |
| Pulser | 0.3/0.4 | 0.3/0.3/0.4 | 0.03 | 0.02 | clean | inside the device's own range |
| Railway | 209.4/206.9 | 208.6/208.4/208.4 | 0.00 | 0.00 | clean | inside the device's own range |
| Rain | 0.1/0.3 | 0.2/0.3/0.3 | 0.05 | 0.06 | clean | inside the device's own range |
| Rainbow | 196.0/196.0 | 196.0/196.0/196.0 | 0.00 | 0.00 | clean | inside the device's own range |
| Rainbow Runner | 240.3/240.7 | 240.5/239.7/240.4 | 0.01 | 0.02 | clean | inside the device's own range |
| Ripple | 0.4/0.2 | 0.5/0.2/0.3 | 0.00 | 0.00 | clean | inside the device's own range |
| Ripple Peak | 1.3/1.8 | 1.2/1.1/1.0 | 0.91 | 0.28 | expected | audio, not scorable on brightness |
| Ripple Rainbow | 16.7/16.7 | 16.7/16.1/15.5 | 0.50 | 0.28 | clean | inside the device's own range |
| Rocktaves | 23.1/1.9 | 23.4/21.9/21.0 | 0.24 | 0.44 | expected | audio, not scorable on brightness |
| Rolling Balls | 28.4/29.7 | 29.2/30.1/29.3 | 0.02 | 0.02 | clean | inside the device's own range |
| Rotozoomer | 233.0/237.5 | 230.6/239.6/234.0 | 0.14 | 0.16 | clean | inside the device's own range |
| Running | 127.6/127.7 | 127.6/127.6/127.6 | 0.00 | 0.00 | clean | inside the device's own range |
| Running Dual | 54.9/55.0 | 54.9/54.9/55.0 | 0.00 | 0.00 | clean | inside the device's own range |
| Saw | 127.8/127.9 | 128.4/128.6/127.8 | 0.00 | 0.00 | clean | inside the device's own range |
| Scan | 63.8/63.8 | 63.8/63.8/63.8 | 0.00 | 0.00 | clean | inside the device's own range |
| Scanner | 4.5/4.5 | 4.3/4.4/4.4 | 0.00 | 0.00 | clean | inside the device's own range |
| Scanner Dual | 24.4/24.5 | 24.3/24.3/24.3 | 0.01 | 0.00 | clean | inside the device's own range |
| Scrolling Text | 5.5/5.5 | 6.6/6.6/6.6 | 0.05 | 0.00 | clean | inside the device's own range |
| Shimmer | 40.8/40.9 | 41.9/41.9/41.7 | 0.01 | 0.00 | clean | inside the device's own range |
| Sindots | 1.4/1.4 | 1.5/1.2/1.5 | 0.05 | 0.07 | clean | inside the device's own range |
| Sine | 82.3/83.0 | 82.8/82.8/82.7 | 0.01 | 0.01 | clean | inside the device's own range |
| Sinelon | 128.4/126.7 | 136.2/125.1/125.8 | 0.07 | 0.10 | clean | inside the device's own range |
| Sinelon Dual | 168.9/170.1 | 168.6/166.1/167.5 | 0.10 | 0.07 | clean | inside the device's own range |
| Soap | 192.8/192.9 | 192.1/195.5/195.2 | 0.08 | 0.13 | clean | ratio 1.01, inside the 0.75 to 1.33 band |
| Solid | 255.0/255.0 | 255.0/255.0/255.0 | 0.00 | 0.00 | clean | inside the device's own range |
| Solid Glitter | 255.0/255.0 | 255.0/255.0/255.0 | 0.00 | 0.00 | clean | inside the device's own range |
| Solid Pattern | 131.5/131.5 | 131.5/131.5/131.5 | 0.00 | 0.00 | clean | inside the device's own range |
| Solid Pattern Tri | 85.2/85.2 | 85.2/85.2/85.2 | 0.00 | 0.00 | clean | inside the device's own range |
| Spaceships | 2.9/2.8 | 3.0/3.0/2.6 | 0.05 | 0.05 | clean | inside the device's own range |
| Sparkle | 0.1/0.1 | 0.0/0.0/0.0 | 0.00 | 0.00 | clean | inside the device's own range |
| Sparkle Dark | 255.0/255.0 | 255.0/255.0/255.0 | 0.00 | 0.00 | clean | inside the device's own range |
| Sparkle+ | 254.4/254.3 | 252.6/253.2/253.3 | 0.00 | 0.00 | clean | inside the device's own range |
| Spots | 53.2/53.2 | 53.2/53.2/53.2 | 0.00 | 0.00 | clean | inside the device's own range |
| Spots Fade | 65.1/70.5 | 67.6/69.7/66.7 | 0.00 | 0.00 | clean | inside the device's own range |
| Squared Swirl | 0.8/0.8 | 0.5/0.4/0.5 | 0.03 | 0.05 | clean | inside the device's own range |
| Stream | 153.7/153.6 | 155.9/159.8/161.5 | 0.05 | 0.03 | clean | ratio 1.04, inside the 0.75 to 1.33 band |
| Stream 2 | 191.9/195.8 | 196.1/194.0/189.6 | 0.11 | 0.10 | clean | inside the device's own range |
| Strobe Mega | 4.4/12.9 | 12.8/12.8/10.6 | 0.00 | 0.00 | clean | inside the device's own range |
| Sun Radiation | 6.6/7.0 | 6.3/6.3/6.8 | 0.00 | 0.00 | clean | inside the device's own range |
| Sunrise | 0.0/0.0 | 0.0/0.0/0.0 | 0.00 | 0.00 | clean | inside the device's own range |
| Sweep | 43.5/175.9 | 40.9/40.9/124.1 | 0.00 | 0.00 | clean | inside the device's own range |
| Swirl | 0.9/0.2 | 0.5/0.6/0.8 | 0.11 | 0.06 | expected | audio, not scorable on brightness |
| Tartan | 129.4/134.8 | 129.1/124.7/131.8 | 0.06 | 0.08 | clean | inside the device's own range |
| Tetrix | 26.6/24.9 | 28.2/32.2/30.3 | 0.21 | 0.09 | clean | ratio 1.18, inside the 0.75 to 1.33 band |
| Theater | 23.2/23.2 | 23.2/23.2/23.2 | 0.00 | 0.00 | clean | inside the device's own range |
| Traffic Light | 98.3/99.8 | 101.6/101.6/101.6 | 0.06 | 0.03 | clean | inside the device's own range |
| Tri Fade | 144.1/172.8 | 143.6/172.2/134.2 | 0.14 | 0.13 | clean | inside the device's own range |
| Twinkle | 0.2/0.2 | 0.2/0.2/0.2 | 0.00 | 0.00 | clean | inside the device's own range |
| Twinklecat | 53.2/54.5 | 54.2/53.6/53.7 | 0.00 | 0.00 | clean | inside the device's own range |
| Twinklefox | 53.4/55.1 | 54.5/53.8/53.8 | 0.00 | 0.00 | clean | inside the device's own range |
| Twinkleup | 41.9/39.7 | 39.8/40.6/42.4 | 0.07 | 0.08 | clean | inside the device's own range |
| Two Dots | 128.5/128.6 | 128.4/128.4/128.4 | 0.00 | 0.00 | clean | inside the device's own range |
| Washing Machine | 161.0/161.0 | 160.9/160.9/160.7 | 0.01 | 0.01 | clean | inside the device's own range |
| Waterfall | 89.3/72.6 | 106.8/106.0/106.6 | 0.58 | 0.18 | expected | audio, not scorable on brightness |
| Waverly | 254.4/239.2 | 162.1/195.4/206.5 | 0.01 | 0.01 | expected | audio, not scorable on brightness |
| Wavesins | 106.6/108.4 | 107.5/111.8/107.0 | 0.12 | 0.13 | clean | inside the device's own range |
| Waving Cell | 226.3/226.3 | 226.2/226.3/226.2 | 0.01 | 0.01 | clean | inside the device's own range |
| Wipe | 150.7/54.1 | 188.0/165.3/69.6 | 0.00 | 0.00 | clean | inside the device's own range |

## 6. The nine effects with no reference at all

Meteor Smooth, Party jerk, Popcorn audio, Multi Comet audio, Fw Starburst
audio, Fireworks audio, GEQ 3D, Paintbrush and Snow Fall exist only in
WLED-MM. The device runs stock WLED 16.0.1 and its 220 slot catalog holds none
of them, so nothing on the device can settle them.

| Effect | Verdict | Evidence |
|---|---|---|
| Snow Fall | clean on the 1D and 2D sweeps, unverified against hardware | animates at 60 and 300 pixels and on the 64x64 panel, not black, not frozen; body diffed against `refs/WLED-MM/wled00/FX.cpp` in round 1. Its `seg.length()` grid sizing is still latent if `m12` ever becomes settable |
| GEQ 3D, Paintbrush | expected | audio driven and MM only; both animate in the simulator |
| Fw Starburst audio | expected, with R2-4's caveat | it is sized from core WLED's budget (68 stars) where MM's own `FAIR_DATA_PER_SEG` would give 8188 bytes and 136. There is no device capture to settle which is right |
| Fireworks audio, Popcorn audio, Multi Comet audio, Meteor Smooth, Party jerk | clean on the sweeps, unverified against hardware | all animate at 60 and 300 pixels and at 64x64 |

All nine carry the moon glyph difference in R2-7 item 2.

## 7. Audio effects, gross failure only

The device has a real microphone in a real room and the port runs
`simulateSound()`. `NOISE.md` shows the device's own two runs differ by up to
107 counts of brightness and 0.40 of coverage on this set, so only black,
frozen for the whole window and wrong axis mean anything.

No audio effect is black on the port, none is frozen for a whole window, none
renders on the wrong axis and none is mirrored or rotated. Puddlepeak, Ripple
Peak and Waterfall, which round 1 found blank, now measure 0.54, 1.44 and 86.4
in the simulator. Eight audio effects flip their `frozen` flag between the two
device runs (Juggles, Noisemove, PS GEQ 1D, PS GEQ 2D, Puddlepeak, Puddles,
Ripple Peak, Rocktaves) and two flip `all_black` (PS Sonic Boom, PS Sonic
Stream), which is the measurement and not the port.

## Counts

| Verdict | Count |
|---|---:|
| defect | 0 |
| defect-watch | 1 |
| artefact | 22 |
| expected | 38 |
| clean | 152 |
| unresolved | 1 |
| total effects with a row here | 214 |

The eight findings in `FINDINGS.md` are counted against effects in
`SUMMARY.md` rather than here, because six of them are not per-effect: R2-1
touches 18 call sites and every particle effect, R2-2 and R2-3 touch 104 and
112 effects through a control rather than a render, R2-4 touches four effects
on three platforms, and R2-5, R2-7 and R2-8 are engine-wide.
