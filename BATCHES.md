# Batch assignment for the parallel effect port

Every one of the 216 effects registered by WLED 16.0.1, plus the nine WLED-MM
exclusives, is accounted for below: already ported, assigned to exactly one batch,
or excluded with a reason. The count check is at the bottom.

Read `PORTING.md` first, in particular section 2 (the registration convention) and
section 8 (the parallel batch workflow). One agent owns one batch, which is one
translation unit, and edits nothing else.

## Batches at a glance

| Batch | Status | Branch and worktree | Translation unit | Group id | Effects | Effect lines | Helper lines |
|---|---|---|---|---|---:|---:|---:|
| `fx_1d_b` | merged | `fx_1d_b` | `components/wled_fx/wf_effects_1d_b.cpp` | `1d_b` | 30 | 637 | 210 |
| `fx_1d_c` | merged | `fx_1d_c` | `components/wled_fx/wf_effects_1d_c.cpp` | `1d_c` | 24 | 696 | 150 |
| `fx_1d_d` | merged | `fx_1d_d` | `components/wled_fx/wf_effects_1d_d.cpp` | `1d_d` | 30 | 767 | 72 |
| `fx_1d_e` | merged | `fx_1d_e` | `components/wled_fx/wf_effects_1d_e.cpp` | `1d_e` | 24 | 614 | 231 |
| `fx_2d` | merged | `fx_2d` | `components/wled_fx/wf_effects_2d_b.cpp` | `2d_b` | 29 | 1178 | 51 |
| `fx_1d2d` | merged | `fx_1d2d` | `components/wled_fx/wf_effects_1d2d.cpp` | `1d2d` | 7 | 468 | 50 |
| `fx_particle_2d` | 2 of 12 done, unblocked | `fx_particle_2d` | `components/wled_fx/wf_effects_particle_2d.cpp` | `particle_2d` | 12 | 999 | 18 |
| `fx_particle_1d` | 1 of 11 done, unblocked | `fx_particle_1d` | `components/wled_fx/wf_effects_particle_1d.cpp` | `particle_1d` | 11 | 921 | 0 |
| `fx_audio_vol` | merged | `fx_audio_vol` | `components/wled_fx/wf_effects_audio_vol.cpp` | `audio_vol` | 16 | 312 | 115 |
| `fx_audio_fft` | merged | `fx_audio_fft` | `components/wled_fx/wf_effects_audio_fft.cpp` | `audio_fft` | 13 | 433 | 82 |
| `fx_audio_particle` | not started, unblocked | `fx_audio_particle` | `components/wled_fx/wf_effects_audio_particle.cpp` | `audio_particle` | 8 | 663 | 0 |
| `fx_mm` | not started | `fx_mm` | `components/wled_fx/wf_effects_mm.cpp` | `mm` | 9 | 408 | 374 |
| **Total** | **186 of 213 registered** | | | | **213** | | **9449** |

Status as of 2026-09-19. The six effect batches plus the particle system engine
and the audio source are merged to `main`. The particle engine landed with three
effects as smoke tests, PS Fire and PS Fireworks in `particle_2d` and PS DripDrop
in `particle_1d`, so those three are already registered and the two particle
batches are no longer blocked. 186 effects are registered: 10 from phase 1, 154
from the six merged batches, 3 particle smoke tests, and the rest to come.

**Read this before starting a batch.** The rule that a translation unit never
shares a helper has been relaxed for helpers that turned out to be shared. The
ones the first wave duplicated now live in the engine, in
`components/wled_fx/wf_fx_shared.h`: `get_random_wheel_index()`,
`tristate_square8()`, `sin_gap()`, `speed_formula_l()` (the old
`SPEED_FORMULA_L` macro), `blink()`, `mode_gravcenter_base()`,
`mode_colorwaves_pride_base()`, `fx_prng()` and the `Ripple`, `Spark`, `Flasher`
and `Gravity` structs, plus `IBN`. `ULTRAWHITE` and `DARKSLATEGRAY` are in
`wf_color.h`, and `FRAMETIME_FIXED`, `NUM_COLORS` and `FAIR_DATA_PER_SEG` are in
`wf_segment.h`. Check there before copying a helper into your own file, and
still list anything new you had to write in your hand-off so the next
integration can absorb it.

Batch names carry an `fx_` prefix; the group id inside the source does not, because
`1d_a` and `2d_a` were already taken by the ten effects ported in phase 1. That is
why the four 1D batches are lettered b to e rather than a to d, and why the 2D batch
is `2d_b`. The 2D batch was checked against the 1800 line guidance and comes in at
about 1229 lines, so it is not split.

Line counts are upstream body lines measured by brace matching, taken from
`recon/wled-inventory.md`. "Helper lines" is the shared upstream code the batch has
to copy into its own translation unit as static functions, because translation units
never share helpers here (see PORTING.md section 6).

## `fx_1d_b` (1D batch B)

Translation unit `components/wled_fx/wf_effects_1d_b.cpp`, group id `1d_b`, 30 effects,
about 637 effect lines plus 210 helper lines.

| WLED ID | Name | Function | Lines |
|---:|---|---|---:|
| 3 | Wipe | `mode_color_wipe` | 3 |
| 4 | Wipe Random | `mode_color_wipe_random` | 3 |
| 6 | Sweep | `mode_color_sweep` | 3 |
| 36 | Sweep Random | `mode_color_sweep_random` | 3 |
| 23 | Strobe | `mode_strobe` | 3 |
| 24 | Strobe Rainbow | `mode_strobe_rainbow` | 3 |
| 26 | Blink Rainbow | `mode_blink_rainbow` | 3 |
| 17 | Twinkle | `mode_twinkle` | 27 |
| 106 | Twinkleup | `mode_twinkleup` | 13 |
| 51 | Fairytwinkle | `mode_fairytwinkle` | 42 |
| 74 | Colortwinkles | `mode_colortwinkle` | 52 |
| 80 | Twinklefox | `mode_twinklefox` | 4 |
| 81 | Twinklecat | `mode_twinklecat` | 4 |
| 46 | Gradient | `mode_gradient` | 3 |
| 47 | Loading | `mode_loading` | 3 |
| 87 | Glitter | `mode_glitter` | 18 |
| 103 | Solid Glitter | `mode_solid_glitter` | 5 |
| 7 | Dynamic | `mode_dynamic` | 28 |
| 117 | Dynamic Smooth | `mode_dynamic_smooth` | 6 |
| 48 | Rolling Balls | `mode_rolling_balls` | 83 |
| 44 | Tetrix | `mode_tetrix` | 72 |
| 58 | ICU | `mode_icu` | 66 |
| 62 | Oscillate | `mode_oscillate` | 47 |
| 104 | Sunrise | `mode_sunrise` | 43 |
| 39 | Stream | `mode_running_random` | 30 |
| 84 | Solid Pattern Tri | `mode_tri_static_pattern` | 21 |
| 111 | Chunchun | `mode_chunchun` | 17 |
| 108 | Sine | `mode_sinewave` | 14 |
| 147 | Perlin Move | `mode_perlinmove` | 9 |
| 184 | Wavesins | `mode_wavesins` | 9 |

Shared upstream helpers this batch owns:

* Wipe, Wipe Random, Sweep, Sweep Random: color_wipe() FX.cpp:262, 49 lines
* Strobe, Strobe Rainbow, Blink Rainbow: blink() FX.cpp:195, 24 lines. Blink itself is already in 1d_a, so copy the base again
* Twinkle, Twinkleup, Fairytwinkle, Colortwinkles, Twinklefox, Twinklecat: twinklefox_base() FX.cpp:2642, 57 lines and twinklefox_one_twinkle() FX.cpp:2580, 56 lines
* Gradient, Loading: gradient_base() FX.cpp:1394, 21 lines
* Glitter, Solid Glitter: glitter_base() FX.cpp:3387, 3 lines
* Dynamic, Dynamic Smooth: Dynamic Smooth calls mode_dynamic directly, so both live here

## `fx_1d_c` (1D batch C)

Translation unit `components/wled_fx/wf_effects_1d_c.cpp`, group id `1d_c`, 24 effects,
about 696 effect lines plus 150 helper lines.

| WLED ID | Name | Function | Lines |
|---:|---|---|---:|
| 18 | Dissolve | `mode_dissolve` | 3 |
| 19 | Dissolve Rnd | `mode_dissolve_random` | 3 |
| 10 | Scan | `mode_scan` | 3 |
| 11 | Scan Dual | `mode_dual_scan` | 3 |
| 40 | Scanner | `mode_larson_scanner` | 41 |
| 60 | Scanner Dual | `mode_dual_larson_scanner` | 4 |
| 92 | Sinelon | `mode_sinelon` | 3 |
| 93 | Sinelon Dual | `mode_sinelon_dual` | 3 |
| 94 | Sinelon Rainbow | `mode_sinelon_rainbow` | 3 |
| 85 | Spots | `mode_spots` | 4 |
| 86 | Spots Fade | `mode_spots_fade` | 7 |
| 113 | Washing Machine | `mode_washing_machine` | 10 |
| 151 | PacMan | `mode_pacman` | 169 |
| 116 | TV Simulator | `mode_tv_simulator` | 99 |
| 96 | Drip | `mode_drip` | 78 |
| 49 | Fairy | `mode_fairy` | 70 |
| 76 | Meteor | `mode_meteor` | 64 |
| 56 | Tri Fade | `mode_tricolor_fade` | 34 |
| 35 | Traffic Light | `mode_traffic_light` | 24 |
| 45 | Fire Flicker | `mode_fire_flicker` | 22 |
| 50 | Two Dots | `mode_two_dots` | 17 |
| 64 | Juggle | `mode_juggle` | 14 |
| 8 | Colorloop | `mode_rainbow` | 10 |
| 12 | Fade | `mode_fade` | 8 |

Shared upstream helpers this batch owns:

* Dissolve, Dissolve Rnd: dissolve() FX.cpp:688, 53 lines
* Scan, Scan Dual: scan() FX.cpp:467, 24 lines
* Scanner, Scanner Dual: Scanner Dual calls mode_larson_scanner directly, so both live here
* Sinelon, Sinelon Dual, Sinelon Rainbow: sinelon_base() FX.cpp:3336, 31 lines
* Spots, Spots Fade: spots_base() FX.cpp:2913, 24 lines
* Washing Machine: tristate_square8() FX.cpp:102, 18 lines

## `fx_1d_d` (1D batch D)

Translation unit `components/wled_fx/wf_effects_1d_d.cpp`, group id `1d_d`, 30 effects,
about 767 effect lines plus 72 helper lines.

| WLED ID | Name | Function | Lines |
|---:|---|---|---:|
| 13 | Theater | `mode_theater_chase` | 3 |
| 14 | Theater Rainbow | `mode_theater_chase_rainbow` | 3 |
| 37 | Chase 2 | `mode_running_color` | 3 |
| 15 | Running | `mode_running_lights` | 3 |
| 16 | Saw | `mode_saw` | 3 |
| 52 | Running Dual | `mode_running_dual` | 3 |
| 31 | Chase Flash | `mode_chase_flash` | 32 |
| 32 | Chase Flash Rnd | `mode_chase_flash_random` | 37 |
| 5 | Random Colors | `mode_random_color` | 25 |
| 101 | Pacifica | `mode_pacifica` | 73 |
| 71 | Noise 2 | `mode_noise16_2` | 13 |
| 72 | Noise 3 | `mode_noise16_3` | 16 |
| 73 | Noise 4 | `mode_noise16_4` | 7 |
| 69 | Fill Noise | `mode_fillnoise8` | 8 |
| 107 | Noise Pal | `mode_noisepal` | 31 |
| 20 | Sparkle | `mode_sparkle` | 14 |
| 21 | Sparkle Dark | `mode_flash_sparkle` | 13 |
| 22 | Sparkle+ | `mode_hyper_sparkle` | 16 |
| 112 | Dancing Shadows | `mode_dancing_shadows` | 113 |
| 161 | Shimmer | `mode_shimmer` | 64 |
| 95 | Popcorn | `mode_popcorn` | 57 |
| 34 | Colorful | `mode_colorful` | 37 |
| 38 | Aurora | `mode_aurora` | 34 |
| 55 | Tri Wipe | `mode_tricolor_wipe` | 32 |
| 110 | Flow | `mode_flow` | 30 |
| 78 | Railway | `mode_railway` | 27 |
| 25 | Strobe Mega | `mode_multi_strobe` | 22 |
| 115 | Blends | `mode_blends` | 19 |
| 2 | Breathe | `mode_breath` | 15 |
| 179 | Flow Stripe | `mode_FlowStripe` | 14 |

Shared upstream helpers this batch owns:

* Theater, Theater Rainbow, Chase 2, Running, Saw, Running Dual: running() FX.cpp:546, 23 lines and running_base() FX.cpp:594, 27 lines
* Chase Flash, Chase Flash Rnd, Random Colors: get_random_wheel_index() FX.cpp:340, 8 lines
* Pacifica: pacifica_one_layer() FX.cpp:4165, 14 lines
* Noise 2, Noise 3, Noise 4, Fill Noise, Noise Pal: the perlin noise family. Noise 1 is already in 1d_a

## `fx_1d_e` (1D batch E)

Translation unit `components/wled_fx/wf_effects_1d_e.cpp`, group id `1d_e`, 24 effects,
about 614 effect lines plus 231 helper lines.

| WLED ID | Name | Function | Lines |
|---:|---|---|---:|
| 28 | Chase | `mode_chase_color` | 3 |
| 29 | Chase Random | `mode_chase_random` | 3 |
| 30 | Chase Rainbow | `mode_chase_rainbow` | 8 |
| 33 | Rainbow Runner | `mode_chase_rainbow_white` | 8 |
| 54 | Chase 3 | `mode_tricolor_chase` | 3 |
| 88 | Candle | `mode_candle` | 4 |
| 102 | Candle Multi | `mode_candle_multi` | 4 |
| 105 | Phased | `mode_phased` | 3 |
| 109 | Phased Noise | `mode_phased_noise` | 3 |
| 67 | Colorwaves | `mode_colorwaves` | 3 |
| 89 | Fireworks Starburst | `mode_starburst` | 108 |
| 91 | Bouncing Balls | `mode_bouncing_balls` | 69 |
| 219 | Slow Transition | `mode_slow_transition` | 77 |
| 218 | Color Clouds | `mode_ColorClouds` | 64 |
| 98 | Percent | `mode_percent` | 44 |
| 57 | Lightning | `mode_lightning` | 39 |
| 27 | Android | `mode_android` | 34 |
| 59 | Multi Comet | `mode_multi_comet` | 30 |
| 100 | Heartbeat | `mode_heartbeat` | 24 |
| 41 | Lighthouse | `mode_comet` | 20 |
| 83 | Solid Pattern | `mode_static_pattern` | 16 |
| 61 | Stream 2 | `mode_random_chase` | 27 |
| 75 | Lake | `mode_lake` | 13 |
| 68 | Bpm | `mode_bpm` | 7 |

Shared upstream helpers this batch owns:

* Chase, Chase Random, Chase Rainbow, Rainbow Runner, Chase 3: chase() FX.cpp:895, 63 lines and tricolor_chase() FX.cpp:1595, 16 lines
* Candle, Candle Multi: candle() FX.cpp:3498, 79 lines
* Phased, Phased Noise: phased_base() FX.cpp:4308, 22 lines and sin_gap() FX.cpp:90, 4 lines
* Colorwaves: mode_colorwaves_pride_base() FX.cpp:1948, 47 lines. Pride 2015 is already in 1d_a, so copy the base again
* Fireworks Starburst: shares no code with the 1D+2D fireworks effects despite the name

## `fx_2d` (2D only)

Translation unit `components/wled_fx/wf_effects_2d_b.cpp`, group id `2d_b`, 29 effects,
about 1178 effect lines plus 51 helper lines.

| WLED ID | Name | Function | Lines |
|---:|---|---|---:|
| 114 | Rotozoomer | `mode_2Dplasmarotozoom` | 38 |
| 118 | Spaceships | `mode_2Dspaceships` | 33 |
| 119 | Crazy Bees | `mode_2Dcrazybees` | 63 |
| 120 | Ghost Rider | `mode_2Dghostrider` | 79 |
| 121 | Blobs | `mode_2Dfloatingblobs` | 89 |
| 123 | Drift Rose | `mode_2Ddriftrose` | 20 |
| 124 | Distortion Waves | `mode_2Ddistortionwaves` | 73 |
| 125 | Soap | `mode_2Dsoap` | 44 |
| 126 | Octopus | `mode_2Doctopus` | 51 |
| 127 | Waving Cell | `mode_2Dwavingcell` | 19 |
| 146 | Noise2D | `mode_2Dnoise` | 15 |
| 149 | Firenoise | `mode_2Dfirenoise` | 25 |
| 150 | Squared Swirl | `mode_2Dsquaredswirl` | 24 |
| 152 | DNA | `mode_2Ddna` | 13 |
| 168 | Julia | `mode_2DJulia` | 99 |
| 172 | Game Of Life | `mode_2Dgameoflife` | 150 |
| 173 | Tartan | `mode_2Dtartan` | 31 |
| 174 | Polar Lights | `mode_2DPolarLights` | 26 |
| 176 | Lissajous | `mode_2DLissajous` | 21 |
| 177 | Frizzles | `mode_2DFrizzles` | 14 |
| 178 | Plasma Ball | `mode_2DPlasmaball` | 29 |
| 180 | Hiphotic | `mode_2DHiphotic` | 13 |
| 181 | Sindots | `mode_2DSindots` | 21 |
| 182 | DNA Spiral | `mode_2DDNASpiral` | 40 |
| 183 | Black Hole | `mode_2DBlackHole` | 26 |
| 166 | Sun Radiation | `mode_2DSunradiation` | 42 |
| 167 | Colored Bursts | `mode_2DColoredBursts` | 44 |
| 162 | Pulser | `mode_2DPulser` | 14 |
| 164 | Drift | `mode_2DDrift` | 22 |

Shared upstream helpers this batch owns:

* Soap: soapPixels() FX.cpp:7834, 51 lines

## `fx_1d2d` (native 1D and 2D)

Translation unit `components/wled_fx/wf_effects_1d2d.cpp`, group id `1d2d`, 7 effects,
about 468 effect lines plus 50 helper lines.

| WLED ID | Name | Function | Lines |
|---:|---|---|---:|
| 42 | Fireworks | `mode_fireworks` | 37 |
| 43 | Rain | `mode_rain` | 31 |
| 79 | Ripple | `mode_ripple` | 9 |
| 99 | Ripple Rainbow | `mode_ripple_rainbow` | 16 |
| 65 | Palette | `mode_palette` | 95 |
| 82 | Halloween Eyes | `mode_halloween_eyes` | 152 |
| 90 | Fireworks 1D | `mode_exploding_fireworks` | 128 |

Shared upstream helpers this batch owns:

* Fireworks, Rain: Rain calls mode_fireworks directly, so both live here
* Ripple, Ripple Rainbow: ripple_base() FX.cpp:2492, 50 lines

## `fx_particle_2d` (particle 2D, non-audio)

Translation unit `components/wled_fx/wf_effects_particle_2d.cpp`, group id `particle_2d`, 12 effects,
about 999 effect lines plus 18 helper lines.

Blocked on phase P3, the particle system. Do not start this batch until
`components/wled_fx/particle/` exists.

| WLED ID | Name | Function | Lines |
|---:|---|---|---:|
| 187 | PS Volcano | `mode_particlevolcano` | 63 |
| 188 | PS Fire | `mode_particlefire` | 95 |
| 189 | PS Fireworks | `mode_particlefireworks` | 133 |
| 190 | PS Vortex | `mode_particlevortex` | 104 |
| 191 | PS Fuzzy Noise | `mode_particleperlin` | 54 |
| 192 | PS Ballpit | `mode_particlepit` | 61 |
| 193 | PS Box | `mode_particlebox` | 79 |
| 194 | PS Attractor | `mode_particleattractor` | 83 |
| 195 | PS Impact | `mode_particleimpact` | 99 |
| 196 | PS Waterfall | `mode_particlewaterfall` | 64 |
| 200 | PS Ghost Rider | `mode_particleghostrider` | 73 |
| 217 | PS Galaxy | `mode_particlegalaxy` | 91 |

Shared upstream helpers this batch owns:

* PS Box: tristate_square8() FX.cpp:102, 18 lines

## `fx_particle_1d` (particle 1D, non-audio)

Translation unit `components/wled_fx/wf_effects_particle_1d.cpp`, group id `particle_1d`, 11 effects,
about 921 effect lines plus 0 helper lines.

Blocked on phase P3, the particle system. Do not start this batch until
`components/wled_fx/particle/` exists.

| WLED ID | Name | Function | Lines |
|---:|---|---|---:|
| 202 | PS DripDrop | `mode_particleDrip` | 103 |
| 203 | PS Pinball | `mode_particlePinball` | 107 |
| 204 | PS Dancing Shadows | `mode_particleDancingShadows` | 107 |
| 205 | PS Fireworks 1D | `mode_particleFireworks1D` | 113 |
| 206 | PS Sparkler | `mode_particleSparkler` | 63 |
| 207 | PS Hourglass | `mode_particleHourglass` | 116 |
| 208 | PS Spray 1D | `mode_particle1Dspray` | 43 |
| 209 | PS 1D Balance | `mode_particleBalance` | 73 |
| 210 | PS Chase | `mode_particleChase` | 88 |
| 211 | PS Starburst | `mode_particleStarburst` | 51 |
| 213 | PS Fire 1D | `mode_particleFire1D` | 57 |

## `fx_audio_vol` (volume reactive, non-particle)

Translation unit `components/wled_fx/wf_effects_audio_vol.cpp`, group id `audio_vol`, 16 effects,
about 312 effect lines plus 115 helper lines.

Audio effects read `seg.audio()`, which returns an `AudioData &` (see
`wf_audio.h`). With no microphone attached that is WLED's simulated sound, so
these effects animate and can be reviewed in the simulator today.

| WLED ID | Name | Function | Lines |
|---:|---|---|---:|
| 156 | Gravcenter | `mode_gravcenter` | 3 |
| 157 | Gravcentric | `mode_gravcentric` | 3 |
| 132 | Gravimeter | `mode_gravimeter` | 3 |
| 134 | Puddles | `mode_puddles` | 3 |
| 144 | Puddlepeak | `mode_puddlepeak` | 3 |
| 148 | Ripple Peak | `mode_ripplepeak` | 66 |
| 128 | Pixels | `mode_pixels` | 21 |
| 129 | Pixelwave | `mode_pixelwave` | 22 |
| 130 | Juggles | `mode_juggles` | 12 |
| 131 | Matripix | `mode_matripix` | 28 |
| 133 | Plasmoid | `mode_plasmoid` | 24 |
| 135 | Midnoise | `mode_midnoise` | 24 |
| 136 | Noisemeter | `mode_noisemeter` | 23 |
| 143 | Noisefire | `mode_noisefire` | 19 |
| 165 | Waverly | `mode_2DWaverly` | 28 |
| 175 | Swirl | `mode_2DSwirl` | 30 |

Shared upstream helpers this batch owns:

* Gravcenter, Gravcentric, Gravimeter: mode_gravcenter_base() FX.cpp:6806, 82 lines. Gravfreq in fx_audio_fft needs its own copy
* Puddles, Puddlepeak: mode_puddles_base() FX.cpp:7125, 33 lines

## `fx_audio_fft` (FFT reactive, non-particle)

Translation unit `components/wled_fx/wf_effects_audio_fft.cpp`, group id `audio_fft`, 13 effects,
about 433 effect lines plus 82 helper lines.

Audio effects read `seg.audio()`, which returns an `AudioData &` (see
`wf_audio.h`). With no microphone attached that is WLED's simulated sound, so
these effects animate and can be reviewed in the simulator today.

| WLED ID | Name | Function | Lines |
|---:|---|---|---:|
| 158 | Gravfreq | `mode_gravfreq` | 3 |
| 137 | Freqwave | `mode_freqwave` | 43 |
| 138 | Freqmatrix | `mode_freqmatrix` | 44 |
| 139 | GEQ | `mode_2DGEQ` | 53 |
| 140 | Waterfall | `mode_waterfall` | 47 |
| 141 | Freqpixels | `mode_freqpixels` | 22 |
| 145 | Noisemove | `mode_noisemove` | 15 |
| 155 | Freqmap | `mode_freqmap` | 25 |
| 159 | DJ Light | `mode_DJLight` | 23 |
| 160 | Funky Plank | `mode_2DFunkyPlank` | 45 |
| 163 | Blurz | `mode_blurz` | 25 |
| 185 | Rocktaves | `mode_rocktaves` | 27 |
| 186 | Akemi | `mode_2DAkemi` | 61 |

Shared upstream helpers this batch owns:

* Gravfreq: mode_gravcenter_base() FX.cpp:6806, 82 lines, copied again into this file
* Akemi: reads its bitmap through pgm_read_byte_near(), which wf_math.h now provides

## `fx_audio_particle` (audio and particle)

Translation unit `components/wled_fx/wf_effects_audio_particle.cpp`, group id `audio_particle`, 8 effects,
about 663 effect lines plus 0 helper lines.

Blocked on phase P3, the particle system. Do not start this batch until
`components/wled_fx/particle/` exists.

Audio effects read `seg.audio()`, which returns an `AudioData &` (see
`wf_audio.h`). With no microphone attached that is WLED's simulated sound, so
these effects animate and can be reviewed in the simulator today.

| WLED ID | Name | Function | Lines |
|---:|---|---|---:|
| 197 | PS Spray | `mode_particlespray` | 65 |
| 198 | PS GEQ 2D | `mode_particleGEQ` | 64 |
| 199 | PS GEQ Nova | `mode_particlecenterGEQ` | 62 |
| 201 | PS Blobs | `mode_particleblobs` | 66 |
| 212 | PS GEQ 1D | `mode_particle1DGEQ` | 65 |
| 214 | PS Sonic Stream | `mode_particle1DsonicStream` | 96 |
| 215 | PS Sonic Boom | `mode_particle1DsonicBoom` | 83 |
| 216 | PS Springy | `mode_particleSpringy` | 162 |

## `fx_mm` (WLED-MM exclusives)

Translation unit `components/wled_fx/wf_effects_mm.cpp`, group id `mm`, 9 effects,
about 408 effect lines plus 374 helper lines.

Source is `refs/WLED-MM` at commit `272dab5939d6d83a7c9a6e31d0a2a628c2d284e0`, not
`refs/WLED`. Three things differ from every other batch:

* WLED-MM is based on the 0.15 engine, so its effect functions are
  `uint16_t mode_x()` and return a frame delay. Drop the return type and the
  `return FRAMETIME;`, exactly as upstream 16.0.1 did when it moved to `void`.
* MM registers a second entry for `mode_popcorn`, `mode_starburst` and
  `mode_fireworks` by refactoring each into a `_core(bool useaudio)`. Copy the core
  into this file and register only the audio entry from it; the non-audio entries
  stay with their upstream batches.
* GEQ 3D and Paintbrush carry their own GPLv3 licence notice upstream. Copy that
  notice onto those two effects, as PLAN.md requires.

| WLED-MM ID | Name | Function | Lines | Notes |
|---:|---|---|---:|---|
| 77 | Meteor Smooth | `mode_meteor_smooth` | 3 | wrapper over mode_meteor_core(bool) at WLED-MM FX.cpp:2537, 67 lines, which you copy into this file |
| 188 | Party jerk | `mode_partyjerk` | 52 | 1D, volume reactive |
| 190 | Popcorn audio | `mode_popcorn_audio` | 1 | wrapper over mode_popcorn_core(bool) at WLED-MM FX.cpp:3335, 104 lines |
| 191 | Multi Comet audio | `mode_multi_comet_ar` | 53 | a real separate function, not a wrapper |
| 192 | Fw Starburst audio | `mode_starburst_audio` | 1 | wrapper over mode_starburst_core(bool) at WLED-MM FX.cpp:3560, 136 lines |
| 194 | Fireworks audio | `mode_fireworks_audio` | 1 | wrapper over mode_fireworks_core(bool) at WLED-MM FX.cpp:1288, 67 lines |
| 195 | GEQ 3D | `mode_GEQLASER` | 133 | 2D, FFT reactive. Carries its own GPLv3 notice upstream, copy it onto the effect |
| 196 | Paintbrush | `mode_2DPaintbrush` | 56 | 2D, FFT reactive. Carries its own GPLv3 notice upstream, copy it onto the effect |
| 197 | Snow Fall | `mode_2DSnowFall` | 108 | 2D, non-audio, heavy float maths |

MM decorates these names with a moon glyph to mark an MM exclusive in its own user
interface. Leave it off here: the name is the registry key, the YAML key and the
`select` option text, and a non-ASCII key is awkward in all three. Register them as
"Meteor Smooth", "Party jerk", "Popcorn audio", "Multi Comet audio",
"Fw Starburst audio", "Fireworks audio", "GEQ 3D", "Paintbrush" and "Snow Fall".

## Already ported in phase 1

| Name | Function | Group |
|---|---|---|
| Solid | `mode_static` | `1d_a` |
| Blink | `mode_blink` | `1d_a` |
| Rainbow | `mode_rainbow_cycle` | `1d_a` |
| Pride 2015 | `mode_pride_2015` | `1d_a` |
| Fire 2012 | `mode_fire_2012` | `1d_a` |
| Noise 1 | `mode_noise16_1` | `1d_a` |
| Plasma | `mode_plasma` | `1d_a` |
| Matrix | `mode_2Dmatrix` | `2d_a` |
| Metaballs | `mode_2Dmetaballs` | `2d_a` |
| Scrolling Text | `mode_2Dscrollingtext` | `2d_a` |

## Excluded

| Name | Function | Why |
|---|---|---|
| Image | `mode_image` | Renders a GIF or BMP off the filesystem through renderImageToSegment(). There is no filesystem, no image decoder and no ledmap story in this component yet. |
| Copy Segment | `mode_copy_segment` | Reads other segments through getSegment(), getSegmentsNum() and setDrawDimensions(). There is one canvas and one segment here, so the effect has nothing to copy from. |

Both are excluded from the count, not silently dropped. If the filesystem and
multi-segment stories ever arrive they get their own small batch.

Upstream's four reserved slots (IDs 142, 169, 170 and 171) hold no effect and are
not counted anywhere; `MODE_COUNT` is 220 but only 216 effects are registered.

## Count check

| Bucket | Count |
|---|---:|
| Registered in WLED 16.0.1 | 216 |
| Ported in phase 1 | 10 |
| Excluded with a reason | 2 |
| Assigned to a batch | 204 |
| **Sum** | **216** |

| Cross-check against the inventory | Expected | Assigned |
|---|---:|---:|
| 1D, non-audio, non-particle | 115 minus 7 ported = 108 | 108 |
| 2D only, non-audio, non-particle | 32 minus 3 ported = 29 | 29 |
| native 1D and 2D | 9 minus 2 excluded = 7 | 7 |
| particle 2D, non-audio | 12 | 12 |
| particle 1D, non-audio | 11 | 11 |
| audio volume, non-particle | 16 | 16 |
| audio FFT, non-particle | 13 | 13 |
| audio and particle | 8 | 8 |
| WLED-MM exclusives, extra, not part of the 216 | 9 | 9 |

