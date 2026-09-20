# Round 1 verdicts

One row per effect examined. `defect` names the finding; `artefact` means the
flag is a measurement problem and says which; `expected` means a legitimate
difference and says why; `clean` means examined and no difference worth acting
on.

Metrics quoted as "ref / port". "bri" is mean brightness out of 255, "chg" is
mean per-frame change, "blobs" is connected lit regions at the reference's
32x32 view. "my dir" is the re-measured direction from adjacent-frame phase
correlation, which does not alias; the shipped report's direction words are not
used as evidence anywhere below.

## 1. The 64 effects offered on a 2D layout

### 2D and 1D+2D, non-audio (39)

| Effect | Verdict | Evidence and reasoning |
|---|---|---|
| Fireworks | clean | bri 0.1 / 0.1, both essentially dark for the whole window at ix=192; hue distance 0.41 comes from 28 against 72 lit pixels in total, far too small a sample to mean anything |
| Rain | clean | bri 0.1 / 0.3, lit-pixel counts 1 to 2 against 0 to 4 over the window; my dir down on both at 21.5 / 20.0 px/s. The report's "front end" attribution rests on an engine capture taken at different settings |
| Ripple | clean | sparse rings on both, bri 0.4 / 0.2, chg 0.24 / 0.15 |
| Ripple Rainbow | clean | bri 16.7 / 16.4, chg 0.65 / 0.65, same ring behaviour |
| Palette | clean | bri 226.2 / 226.2, my dir right on both at 7.3 / 7.8 px/s |
| Halloween Eyes | clean | both sides all black for the whole 6 s (max brightness 0 on both). The effect idles for long random intervals; agreement is what matters |
| Fireworks 1D | defect F2 (latent) | the spark pool is a quarter of upstream's, but `nSparks` is capped by flare height (63) below the port's cap (102) at 64x64, so nothing is visible here. Explosion shapes and counts agree |
| Matrix | clean | blobs 21, 25, 33, 37, 40 against 24, 28, 34, 39, 44; same spawn rate, same green trail |
| Metaballs | clean | bri 72.6 / 73.1, same three white dots on the same red field |
| Scrolling Text | expected | different text by design (deviation 4); both static because the string fits the panel |
| Black Hole | clean | bri 4.2 / 4.2, same ring radii, same rainbow at pal=11 |
| Colored Bursts | clean | bri 34.3 / 34.3, chg 14.64 / 15.47, same cyan and white rays |
| DNA | clean | bri 6.3 / 6.5, identical double helix |
| DNA Spiral | clean | bri 38.5 / 34.7, same spiral, phase differs |
| Drift | clean | bri 9.6 / 9.5, chg 10.55 / 10.76 |
| Firenoise | clean | bri 119.6 / 119.6, top-bottom symmetry 0.64 / 0.64 |
| Frizzles | clean | bri 1.3 / 1.5, same sparse diagonal strokes |
| Game Of Life | artefact | the original reference frame is not reproducible. Two fresh device captures (`devA`, `devB`) at the same pal=11 render the same saturated rainbow the port does |
| Hiphotic | clean | bri 228.0 / 227.8, identical at every pixel-value quantile |
| Julia | clean | visually frame-for-frame identical, bri 138.2 / 140.3 |
| Lissajous | clean | bri 14.2 / 12.7, same figure, phase differs |
| Noise2D | clean | bri 228.3 / 227.9, identical at every quantile |
| Plasma Ball | clean | bri 34.0 / 36.9, same tendrils and same colour sweep |
| Polar Lights | clean | bri 31.5 / 31.6, chg 6.39 / 6.58 |
| Pulser | clean | bri 0.3 / 0.3, same sparse pulses |
| Sindots | clean | bri 1.4 / 1.5, same ring of dots |
| Squared Swirl | clean | bri 0.8 / 0.5, same sparse diagonal |
| Sun Radiation | clean | bri 6.6 / 6.9, same central blob and edge artefacts |
| Tartan | artefact | driven by `beatsin16_t(3,..)` and `beatsin16_t(2,..)`, periods of 20 and 30 s, sampled in a 6 s window; bri 129.4 / 130.6, and the chg gap 10.93 / 6.83 is where in those beats each capture started |
| Spaceships | artefact | same ships on the same arc; the 1.64x ratio comes from a 15 to 25 px/s estimate at confidence under 7 on an aliasing pattern |
| Crazy Bees | expected | both saturated rainbow at pal=11, sampled colours (0,255,180) and (255,133,0) against (255,242,0) and (0,93,255): different random draws of the same palette |
| Ghost Rider | clean | bri 4.5 / 4.0, same comet and same trail |
| Blobs | expected | same blob count, sizes and motion; colours are per-blob random draws |
| Drift Rose | clean | bri 5.6 / 5.7, chg 2.24 / 2.26 |
| Rotozoomer | clean | bri 233.0 / 235.7, same zoom ramp |
| Distortion Waves | artefact | bri 206.6 / 206.2, chg 37.83 / 36.01; the pattern is at the Nyquist limit of the 32x32 live view, so the direction estimate is meaningless |
| Soap | clean | bri 192.8 / 194.6, chg 41.85 / 45.25, same swirl character |
| Octopus | clean | bri 112.2 / 111.9, same arms and same rotation |
| Waving Cell | artefact | bri 226.3 / 226.3 and dominant temporal frequency 0.835 Hz against 0.830 Hz; the 16.3 against 1.1 px/s figure is the estimator failing on a one-pixel checkerboard |

### 2D particle, non-audio (15)

| Effect | Verdict | Evidence and reasoning |
|---|---|---|
| PS Fire | defect F1 | bri 112.0 / 93.3 (0.83); WLED's flames reach visibly higher |
| PS Fireworks | clean | both fire one rocket and then idle for the rest of the window; bri 0.4 / 0.1 over about 50 lit pixels, so the hue distance of 1.00 is noise |
| PS Vortex | defect F1 | bri 79.2 / 50.1 (0.63); same spiral, WLED's arms carry a soft halo the port's lack |
| PS Volcano | defect F1 | bri 6.1 / 5.2 (0.85); same plume shape and growth |
| PS Ballpit | defect F1 | bri 3.1 / 1.6 (0.52). Blob counts prove the emission rate is identical (1.46 per second on both), so the whole gap is brightness |
| PS Waterfall | defect F1 | bri 63.6 / 43.5 (0.69); same fall, same splash line |
| PS Box | defect F1 | bri 19.8 / 13.9 (0.70). The pile forms against a different wall at 6 s because `seg.aux0` starts at `hw_random16()`; the tilt code is a faithful copy |
| PS Fuzzy Noise | defect F1 | bri 118.0 / 88.8 (0.75) |
| PS Impact | defect F1 | bri 18.9 / 10.4 (0.55) |
| PS Attractor | defect F1 | bri 4.8 / 2.9 (0.61); same swarm shape |
| PS Ghost Rider | defect F1 | bri 36.4 / 29.3 (0.81). The hue distance of 0.96 is separate and expected: the effect defaults to pal=1, Random Cycle |
| PS Galaxy | clean | both show only the core after 6 s (bri 1.7 / 1.4); the engine row shows a full spiral only because the simulator gives it a 1500 ms head start from its PACING table |
| PS Spray | expected | audio, and it branches on `has_real_audio()`; the device takes the audio branch and the port the fallback |
| PS Blobs | expected | same, `has_real_audio()` branch; bri 35.2 / 15.1 |
| PS GEQ Nova | expected + F1 | audio driven; bri 34.8 / 26.4 is in line with the F1 ratio |

### 2D audio (6 with a reference)

| Effect | Verdict | Evidence and reasoning |
|---|---|---|
| GEQ | expected | 16 bars on both, contiguous 4-pixel bars on both (column lit-profile is flat on both sides, so the black gaps in the port's row are empty bands, not a bar-width bug). Heights differ because one side hears a room |
| Funky Plank | expected | same scrolling GEQ rows; column profiles flat on both. Port lit 59 percent against 85 percent because simulated bands hit zero and a real room's never do |
| Akemi | clean | bri 52.1 / 56.2, lit 0.44 / 0.46, same character |
| PS GEQ 2D | expected | audio driven; bri 10.1 / 8.4 |
| Swirl | expected | bri 0.9 / 0.7, both nearly dark; volume driven |
| Waverly | expected | port oscillates between full and mostly black because `simulateSound()` swings hard; the device's mic gives a steady floor |

### 2D with no reference at all (4)

| Effect | Verdict | Evidence and reasoning |
|---|---|---|
| Snow Fall | defect F4 (coverage) | WLED-MM only, absent from the device's 220-slot catalog, never captured |
| GEQ 3D | defect F4 (coverage) | same |
| Paintbrush | defect F4 (coverage) | same |
| Fireworks audio | defect F4 (coverage) | same |

## 2. Flagged non-audio effects outside the 2D set

| Effect | Verdict | Evidence and reasoning |
|---|---|---|
| Fireworks Starburst | defect F2 | blobs 44, 39, 50, 28, 49, 36 against 10, 26, 33, 25, 36, 23 |
| PS Starburst | defect F1 | bri 8.2 / 3.8 (0.47) |
| PS Springy | defect F1 | bri 56.6 / 40.2 (0.71) |
| PS Spray 1D | defect F1 | bri 2.2 / 1.5 (0.65) |
| PS 1D Balance | defect F1 | bri 46.0 / 41.0 (0.89) |
| PS Hourglass | clean | bri 120.73 / 120.69 |
| PS Chase | clean | bri 53.84 / 53.83 |
| PS Dancing Shadows | clean | bri 6.79 / 7.02 |
| PS Fire 1D | clean | bri 8.16 / 7.93 |
| PS Fireworks 1D | clean | bri 2.31 / 2.61 |
| PS DripDrop | artefact | both move left; 40.7 against 78.4 px/s is the estimator aliasing on a fast repeating pattern |
| PS Pinball | artefact | bri 1.1 / 1.7 over about 1 percent coverage; too little signal for any estimator |
| Aurora | artefact | palette latched at `call == 0` during WLED's palette cross-fade. Recaptured with the palette settled (`devD`) the device renders the port's green |
| Color Clouds | artefact | the reference's grey floor is upstream's uninitialised white channel surfacing through the live view's RGBW to RGB map. Reproduced on the device (`devC`); see F3 item 1 |
| Slow Transition | expected | the port has no palette transition, so the effect's `changed` test never fires and it holds a constant frame. WLED's keeps firing for seconds after the change |
| Sweep | expected | identical sweep rate, 0.100 against 0.099 frame-fractions per second; the 17 against 84 percent coverage is where in a period longer than the window each capture started |
| Wipe | expected | identical rate, 0.102 against 0.100 per second; same phase argument |
| Wipe Random | artefact | bri 187.7 / 184.3, both fully lit, and the hue distance of 1.00 is one random colour against another |
| Sweep Random | artefact | same, bri 154.4 / 177.1 |
| Scan Dual | expected | a bouncing scanner; direction depends on which half of the bounce the window caught. Rates 0.031 against 0.034 per second |
| Rainbow | artefact | dominant temporal frequency 3.357 Hz against 3.316 Hz, a 1 percent match; the 19x speed ratio came from phase-correlating a spatially uniform frame |
| Theater Rainbow | artefact | dominant frequency 0.501 against 0.498 Hz; hue distance 1.00 over 9 percent coverage |
| Chase Random | expected | bri 217.9 / 227.6, same chase; hue 0.74 is one random colour against another |
| Chase Rainbow | artefact | chg 105.9 / 108.7, same strobing chase; the estimator cannot track it |
| TV Simulator | expected | a random sequence of simulated scenes; bri 186.6 / 174.1 |
| Perlin Move | artefact | bri 21.6 / 21.4, chg 0.48 / 0.55; both barely move and the "one side is still" flag is the 0.5 frozen threshold splitting them |
| Pride 2015 | unresolved | bri 124.8 against 73.6 is a real 40 percent gap with no obvious cause; both animate, hue distance 0.13. Not a palette or a settings difference. Carried to round 2 |
| PS Sonic Stream | unresolved | the device shows almost nothing (bri 0.08) and the port plenty (34.5). Consistent with a quiet room, but the effect is also the subject of deviation 21, so it should be checked with music playing |
| Bouncing Balls, Juggle, Glitter, Sparkle, Dissolve Rnd, Noise 1, Shimmer, Dancing Shadows, Sine, Fire 2012, Meteor, Chase 2, Chunchun, Colorwaves, Rainbow Runner, Sinelon Rainbow, Sinelon Dual, Washing Machine, Android, Tri Fade, Drift, Firenoise, Hiphotic, Noise2D, Squared Swirl, Sindots | artefact | all flagged only on direction or speed from the aliasing estimator, and all have matching brightness, coverage and hue. Re-measured with adjacent-frame correlation they agree or the estimate has no confidence |

## 3. Audio effects, gross-failure sweep only

Every audio effect was checked for black, frozen, wrong axis or a missing
render. Three results worth recording:

| Effect | Verdict | Evidence and reasoning |
|---|---|---|
| Puddlepeak | defect F5 | port bri 0.01 and chg 0.00 against 2.46 and 0.58: effectively blank |
| Ripple Peak | defect F5 | port bri 0.39 against 1.28, same `sample_peak` gate |
| Waterfall | defect F5 | port chg 6.01 against 37.59, same gate |
| PS Sonic Boom | expected | the device is all black (quiet room), the port animates |
| Blurz, Freqmap, Freqpixels, Juggles, Noisemove, Swirl | expected | both sides dim or near-dim; the differences track volume, not structure |
| Gravcenter, Gravfreq, Gravimeter, Matripix, Noisemeter, Plasmoid, Freqwave, Freqmatrix, DJ Light, Rocktaves, Midnoise, Pixels, Pixelwave, Noisefire, GEQ, Funky Plank, Akemi, Swirl, Waverly, PS GEQ 1D, PS GEQ 2D, PS GEQ Nova, PS Spray, PS Blobs, PS Sonic Stream | expected | none is black, frozen, mirrored or rotated; all render on the correct axis. Brightness and coverage gaps track the real microphone against `simulateSound()` |
