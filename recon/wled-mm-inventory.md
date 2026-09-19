# WLED-MM (MoonModules) effect inventory

Recon only. Written 2026-09-19 for the ESPHome WLED effects port.

## 1. Source under review

| Item | Value |
| --- | --- |
| Repo | [MoonModules/WLED-MM](https://github.com/MoonModules/WLED-MM) |
| Default branch | `mdev` |
| Commit | `272dab5939d6d83a7c9a6e31d0a2a628c2d284e0` |
| Commit date | 2026-09-04 |
| Commit subject | Prevent unsigned wrap-around in getLastActiveSegmentId |
| Local clone | `C:\Users\bharv\development\esphome-wled-fx\refs\WLED-MM` (shallow, depth 1) |
| Upstream compared against | `C:\Users\bharv\development\esphome-wled-fx\refs\WLED` at tag `v16.0.1`, commit `29b389df1c1aaec6ff53aea742d17063b985906c` (read only, cloned by the other agent) |

Sizes: MM `wled00/FX.cpp` is 12,452 lines and `wled00/FX.h` is 1,332 lines. Upstream 16.0.1 is 11,224 and 1,060.

Two external dependencies are pulled at build time and are not in the clone:

| Dependency | Source | Pin | Purpose |
| --- | --- | --- | --- |
| AudioReactive usermod | [MoonModules/WLED-AudioReactive-Usermod](https://github.com/MoonModules/WLED-AudioReactive-Usermod) | `171c0bbc2bc47e2c11ae203dd00beed82865df2b` | FFT and volume data. Registers no effects. |
| ANIMartRIX library | [netmindz/animartrix](https://github.com/netmindz/animartrix) | `81eb09b91c8c9c8c01f8ea442787f8127d56c72f` | Math engine behind the 51 animartrix effects. Fork of Stefan Petrick's library with a PSRAM allocator. |

## 2. Counts

| Bucket | Count |
| --- | --- |
| Effects registered in `wled00/FX.cpp` | 220 |
| `usermods/artifx/usermod_v2_artifx.h` | 1 |
| `usermods/usermod_v2_animartrix/usermod_v2_animartrix.h` | 51 |
| `usermods/usermod_v2_games/usermod_v2_games.h` | 3 |
| **Total** | **275** |
| Upstream 16.0.1 registered in FX.cpp | 215 |

Category breakdown of the 220 core effects (an effect can be in several buckets; the flags field is the fourth semicolon-delimited field of the metadata string, and an absent flags field means 1D):

| Category | Count |
| --- | --- |
| 1D | 166 |
| also usable in 0D (PWM, relay) | 25 |
| 2D | 63 |
| audio volume (`v`) | 23 |
| audio FFT (`f`) | 21 |
| particle system (2D and 1D) | 31 |

Total body length of the 220 core effect functions is roughly 8,744 lines. The largest are Game Of Life (203), PS Springy (162), Fireworks 1D (135), PS Fireworks (134), GEQ 3D (133), Scrolling Text (129).

MM marks its own additions and its own modifications in the UI with a trailing moon glyph in the effect name. 73 of the 275 carry it: 18 in core FX.cpp, 1 ARTI-FX, 51 animartrix, 3 games.

## 3. MM-exclusive effects

"Exclusive" here means the effect function does not exist in upstream 16.0.1 at all.

### 3a. Core FX.cpp, MM only

| ID | Name | Function | Category | Metadata | ~Lines |
| --- | --- | --- | --- | --- | --- |
| 77 | Meteor Smooth | `mode_meteor_smooth` | 1D | `Meteor Smooth@!,Trail,,,,Gradient;;!;1` | 3 |
| 188 | Party jerk | `mode_partyjerk` | 1D, audio volume | `Party jerk@Effect speed,Sensitivity,Color change speed,Effect speed active multiplier;!,!;!;1v;c1=8,c2=48,m12=0,si=0` | 52 |
| 190 | Popcorn audio ☾ | `mode_popcorn_audio` | 1D, audio volume | `Popcorn audio ☾@!,!,,,,,Overlay;!,!,!;!;1v,1.5d;m12=1` | 1 |
| 191 | Multi Comet audio ☾ | `mode_multi_comet_ar` | 1D, audio volume | `Multi Comet audio ☾@Speed,Tail Length;!,!;!;1v;sx=160,ix=32,m12=7,si=1` | 53 |
| 192 | Fw Starburst audio ☾ | `mode_starburst_audio` | 1D, audio volume | `Fw Starburst audio ☾@Chance,Fragments,,,,,Overlay;,!;!;1v;pal=11,m12=0` | 1 |
| 194 | Fireworks audio ☾ | `mode_fireworks_audio` | 1D, 2D, audio volume | `Fireworks audio ☾@,Frequency;!,!;!;1v,12;ix=192,pal=11` | 1 |
| 195 | GEQ 3D ☾ | `mode_GEQLASER` | 2D, audio FFT | `GEQ 3D ☾@Speed,Front Fill,Horizon,Depth,Num Bands,Borders,Soft,;!,,Peaks;!;2f;sx=255,ix=228,c1=255,c2=255,c3=15,pal=11` | 133 |
| 196 | Paintbrush ☾ | `mode_2DPaintbrush` | 2D, audio FFT | `Paintbrush ☾@Oscillator Offset,# of lines,Fade Rate,,Min Length,Color Chaos,Anti-aliasing,Phase Chaos;!,,Peaks;!;2f;sx=160,ix=255,c1=80,c2=255,c3=0,pal=72,o1=0,o2=1,o3=0` | 56 |
| 197 | Snow Fall ☾ | `mode_2DSnowFall` | 2D | `Snow Fall ☾@!,Spawn Rate,Despawn Rate,Blur,Sway Chance,Use Palette,Inverted Overlay,Prevent Overflow,;!,!;!;2;sx=128,ix=16,c1=17,c2=0,c3=0,o1=0,o2=0,o3=1` | 108 |

Notes on that table:

* **Meteor Smooth** is not a new MM effect, it is an effect upstream deleted. Upstream 16.0.1 still ships `mode_meteor_smooth` in the source but the registration is commented out at `FX.cpp:11058` with the note "merged with mode_meteor". MM still registers it at ID 77. Treat it as a divergence, not an invention.
* **Popcorn audio**, **Fw Starburst audio**, **Fireworks audio** are one-line wrappers. MM refactored `mode_popcorn`, `mode_starburst` and `mode_fireworks` into `mode_popcorn_core(bool)`, `mode_starburst_core(bool)` and `mode_fireworks_core(bool)`, then registered a second entry with the audio flag set. The line counts to budget are the core functions: `mode_popcorn_core` at `FX.cpp:3335` is 104 lines, `mode_starburst_core` at `FX.cpp:3560` is 136 lines, `mode_fireworks_core` at `FX.cpp:1288` is 67 lines.
* **Multi Comet audio** is a real separate 53-line function, not a wrapper.
* **GEQ 3D** and **Paintbrush** are the two effects with a non-EUPL license notice. See section 6.
* **Snow Fall** is the only MM-exclusive 2D non-audio effect of any size (108 lines) and it uses float math heavily (13 float declarations).
* **Party jerk** is the only MM-exclusive core effect with no moon glyph in its name.

### 3b. ARTI-FX usermod (1 effect)

| ID | Name | Function | Category | Metadata | ~Lines |
| --- | --- | --- | --- | --- | --- |
| 187 | ⚙️ ARTI-FX ☾ | `mode_ARTIFX` | 1D | `⚙️ ARTI-FX ☾@Speed,Intensity,Custom 1, Custom 2, Custom 3;!;!;1;mp12=0` | 87 |

ARTI-FX is not a single effect, it is an interpreter. `mode_ARTIFX` runs scripts fetched from [MoonModules/MM-Effects](https://github.com/MoonModules/MM-Effects) (the ARTIFX/wled folder) and rendered through `Segment::jsonToPixels()`. Porting it means porting a scripting VM plus a file downloader, not an effect. Out of scope for an effects port.

### 3c. ANIMartRIX usermod (51 effects)

Each function is a thin wrapper of the shape `anim.initEffect(); anim.<Name>(); return FRAMETIME;`, so the line counts below are 4 to 6 each. All of the real work is in the external `netmindz/animartrix` library. These are polar-coordinate float noise fields; there is no integer path. See the license blocker in section 6.

| ID | Name | Function | Category | Metadata | ~Lines |
| --- | --- | --- | --- | --- | --- |
| 203 | Y💡Module_Experiment10 ☾ | `mode_Module_Experiment10` | 2D | `Y💡Module_Experiment10 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 204 | Y💡Module_Experiment9 ☾ | `mode_Module_Experiment9` | 2D | `Y💡Module_Experiment9 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 205 | Y💡Module_Experiment8 ☾ | `mode_Module_Experiment8` | 2D | `Y💡Module_Experiment8 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 206 | Y💡Module_Experiment7 ☾ | `mode_Module_Experiment7` | 2D | `Y💡Module_Experiment7 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 207 | Y💡Module_Experiment6 ☾ | `mode_Module_Experiment6` | 2D | `Y💡Module_Experiment6 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 208 | Y💡Module_Experiment5 ☾ | `mode_Module_Experiment5` | 2D | `Y💡Module_Experiment5 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 209 | Y💡Module_Experiment4 ☾ | `mode_Module_Experiment4` | 2D | `Y💡Module_Experiment4 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 210 | Y💡Zoom2 ☾ | `mode_Zoom2` | 2D | `Y💡Zoom2 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 211 | Y💡Module_Experiment3 ☾ | `mode_Module_Experiment3` | 2D | `Y💡Module_Experiment3 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 212 | Y💡Module_Experiment2 ☾ | `mode_Module_Experiment2` | 2D | `Y💡Module_Experiment2 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 213 | Y💡Module_Experiment1 ☾ | `mode_Module_Experiment1` | 2D | `Y💡Module_Experiment1 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 214 | Y💡Parametric_Water ☾ | `mode_Parametric_Water` | 2D | `Y💡Parametric_Water ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 215 | Y💡Water ☾ | `mode_Water` | 2D | `Y💡Water ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 216 | Y💡Complex_Kaleido_6 ☾ | `mode_Complex_Kaleido_6` | 2D | `Y💡Complex_Kaleido_6 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 217 | Y💡Complex_Kaleido_5 ☾ | `mode_Complex_Kaleido_5` | 2D | `Y💡Complex_Kaleido_5 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 218 | Y💡Complex_Kaleido_4 ☾ | `mode_Complex_Kaleido_4` | 2D | `Y💡Complex_Kaleido_4 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 219 | Y💡Complex_Kaleido_3 ☾ | `mode_Complex_Kaleido_3` | 2D | `Y💡Complex_Kaleido_3 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 220 | Y💡Complex_Kaleido_2 ☾ | `mode_Complex_Kaleido_2` | 2D | `Y💡Complex_Kaleido_2 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 221 | Y💡Complex_Kaleido ☾ | `mode_Complex_Kaleido` | 2D | `Y💡Complex_Kaleido ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 222 | Y💡SM10 ☾ | `mode_SM10` | 2D | `Y💡SM10 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 223 | Y💡SM9 ☾ | `mode_SM9` | 2D | `Y💡SM9 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 224 | Y💡SM8 ☾ | `mode_SM8` | 2D | `Y💡SM8 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 226 | Y💡SM6 ☾ | `mode_SM6` | 2D | `Y💡SM6 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 227 | Y💡SM5 ☾ | `mode_SM5` | 2D | `Y💡SM5 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 228 | Y💡SM4 ☾ | `mode_SM4` | 2D | `Y💡SM4 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 229 | Y💡SM3 ☾ | `mode_SM3` | 2D | `Y💡SM3 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 230 | Y💡SM2 ☾ | `mode_SM2` | 2D | `Y💡SM2 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 231 | Y💡SM1 ☾ | `mode_SM1` | 2D | `Y💡SM1 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 232 | Y💡Big_Caleido ☾ | `mode_Big_Caleido` | 2D | `Y💡Big_Caleido ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 233 | Y💡RGB_Blobs5 ☾ | `mode_RGB_Blobs5` | 2D | `Y💡RGB_Blobs5 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 234 | Y💡RGB_Blobs4 ☾ | `mode_RGB_Blobs4` | 2D | `Y💡RGB_Blobs4 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 235 | Y💡RGB_Blobs3 ☾ | `mode_RGB_Blobs3` | 2D | `Y💡RGB_Blobs3 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 236 | Y💡RGB_Blobs2 ☾ | `mode_RGB_Blobs2` | 2D | `Y💡RGB_Blobs2 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 237 | Y💡RGB_Blobs ☾ | `mode_RGB_Blobs` | 2D | `Y💡RGB_Blobs ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 238 | Y💡Polar_Waves ☾ | `mode_Polar_Waves` | 2D | `Y💡Polar_Waves ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 239 | Y💡Slow_Fade ☾ | `mode_Slow_Fade` | 2D | `Y💡Slow_Fade ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 240 | Y💡Zoom ☾ | `mode_Zoom` | 2D | `Y💡Zoom ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 241 | Y💡Hot_Blob ☾ | `mode_Hot_Blob` | 2D | `Y💡Hot_Blob ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 242 | Y💡Spiralus2 ☾ | `mode_Spiralus2` | 2D | `Y💡Spiralus2 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 243 | Y💡Spiralus ☾ | `mode_Spiralus` | 2D | `Y💡Spiralus ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 244 | Y💡Yves ☾ | `mode_Yves` | 2D | `Y💡Yves ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 245 | Y💡Scaledemo1 ☾ | `mode_Scaledemo1` | 2D | `Y💡Scaledemo1 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 246 | Y💡Lava1 ☾ | `mode_Lava1` | 2D | `Y💡Lava1 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 247 | Y💡Caleido3 ☾ | `mode_Caleido3` | 2D | `Y💡Caleido3 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 248 | Y💡Caleido2 ☾ | `mode_Caleido2` | 2D | `Y💡Caleido2 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 249 | Y💡Caleido1 ☾ | `mode_Caleido1` | 2D | `Y💡Caleido1 ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 250 | Y💡Distance_Experiment ☾ | `mode_Distance_Experiment` | 2D | `Y💡Distance_Experiment ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 251 | Y💡Center_Field ☾ | `mode_Center_Field` | 2D | `Y💡Center_Field ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 252 | Y💡Waves ☾ | `mode_Waves` | 2D | `Y💡Waves ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 253 | Y💡Chasing_Spirals ☾ | `mode_Chasing_Spirals` | 2D | `Y💡Chasing_Spirals ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |
| 254 | Y💡Rotating_Blob ☾ | `mode_Rotating_Blob` | 2D | `Y💡Rotating_Blob ☾@Speed,,,,,,Gamma Correction;;1;2;o2=0` | 5 |

### 3d. Games usermod (3 effects)

| ID | Name | Function | Category | Metadata | ~Lines |
| --- | --- | --- | --- | --- | --- |
| 255 | 🎮 Pong ☾ | `mode_pongGame` | 2D | `🎮 Pong ☾@!;!;!;2` | 92 |
| 255 | 🎮 IMUTest ☾ | `mode_IMUTest` | 2D | `🎮 IMUTest ☾@;;;2d` | 29 |
| 255 | 🎮 3DIMUCube ☾ | `mode_3DIMUCube` | 2D | `🎮 3DIMUCube ☾@,Perspective;!;!;2;pal=1` | 47 |

All three request ID 255, which means "put me in the first free slot" (see section 5 on `addEffect`). IMUTest and 3DIMUCube need the MPU6050 IMU usermod. These are toys, not effects.

## 4. Effects present in both but materially different

Zero of the 211 shared effect functions are byte identical, because upstream 16.0.1 changed every effect signature from `uint16_t mode_x(void)` returning `FRAMETIME` to `void mode_x(void)`. That alone accounts for the floor. The table below lists the ones where the body genuinely diverges, measured as changed lines after stripping comments and blanks.

| Effect | Function | MM lines | Upstream lines | Changed | What differs |
| --- | --- | --- | --- | --- | --- |
| GEQ | `mode_2DGEQ` | 75 | 43 | 55 | MM adds a "flat mode": when the segment is not 2D or is narrower than 3 pixels it lays the bars out along the strip with a centering offset. MM also buffers the FFT result with `memcpy` and smooths adjacent bar heights. Upstream has neither. MM's metadata also renames sliders. |
| Game Of Life | `mode_2Dgameoflife` | 166 | 122 | 170 | Different data model. MM allocates a `Cell` struct array (2 bytes each) plus 6 bytes of header via a `GameOfLifeGrid` helper class, and exposes Color Mutation, All Colors, Overlay BG and Wrap as UI controls. Upstream's rewrite uses a different buffer layout and a different control set (Blur, Mutation). The two are not interchangeable and the preset payloads are not compatible. |
| Scrolling Text | `mode_2Dscrollingtext` | 114 | 148 | 191 | MM carries the full-font option (`WLED_ENABLE_FULL_FONTS`, `src/font/codepages.h`) that upstream does not have. Largest structural divergence in the file. |
| Halloween Eyes | `mode_halloween_eyes` | 49 | 115 | 109 | Upstream rewrote and expanded it; MM has the older, shorter version. |
| Polar Lights | `mode_2DPolarLights` | 61 | 22 | 56 | MM keeps the long original; upstream collapsed it. |
| Gravimeter, Gravcenter, Gravcentric, Gravfreq | `mode_grav*` | 32 to 55 | 3 | 31 to 54 | Upstream factored all four into `mode_gravcenter_base(int)` wrappers. MM still has four full standalone implementations. Same visual family, completely different code shape. |
| Pride 2015, Colorwaves | `mode_pride_2015`, `mode_colorwaves` | 32 to 36 | 3 | 31 to 35 | Same story: upstream shares `mode_colorwaves_pride_base(bool)`, MM does not. |
| Meteor | `mode_meteor` | 3 | 58 | 57 | Inverted: upstream merged Meteor Smooth into Meteor and grew the function; MM's Meteor is a thin wrapper and Meteor Smooth is separate. |
| Popcorn, Fireworks, Fireworks Starburst | `mode_popcorn`, `mode_fireworks`, `mode_starburst` | 1 | 33 to 83 | 33 to 83 | MM wrappers around `*_core(bool useaudio)`. Logic is equivalent to upstream plus an audio branch. |
| Drip | `mode_drip` | 84 | 64 | 50 | MM version has extra gravity and audio handling. |
| Blurz Plus | `mode_blurz` | 51 | 19 | 45 | MM renamed it and added options; upstream shrank it. |
| Matripix | `mode_matripix` | 35 | 22 | 31 | MM adds options. |
| DJ Light | `mode_DJLight` | 44 | 16 | 32 | MM keeps the longer original. |
| Akemi | `mode_2DAkemi` | 3 | 51 | 50 | MM wrapper, upstream inline. |
| ICU | `mode_icu` | 28 | 52 | 53 | Upstream rewrote. |
| Drift | `mode_2DDrift` | 37 | 19 | 44 | Upstream collapsed. |
| Octopus | `mode_2Doctopus` | 64 | 42 | 42 | MM adds a RadialWave option. |
| Matrix | `mode_2Dmatrix` | 50 | 52 | 41 | Independent rewrites on both sides. |
| Colortwinkles | `mode_colortwinkle` | 50 | 45 | 37 | Independent rewrites. |
| Palette | `mode_palette` | 14 | 67 | 65 | Upstream grew it substantially (gradient and shift handling). |
| PS Pinball | `mode_particlePinball` | 88 | 91 | 36 | Follows the particle system divergence in section 5. |
| Lissajous | `mode_2DLissajous` | 27 | 22 | 22 | MM version is moon-marked, meaning MM changed it deliberately. |
| Stream, Stream 2 | `mode_running_random`, `mode_random_chase` | 25 to 28 | 24 to 27 | small | Moon-marked, small deliberate MM tweaks. |
| Waverly | `mode_2DWaverly` | 31 | 22 | 24 | Moon-marked MM tweak. |
| Exploding Fireworks | `mode_exploding_fireworks` | 117 | 111 | 44 | Both sides drifted. |
| Crazy Bees | `mode_2Dcrazybees` | 59 | 56 | 31 | Both sides drifted. |
| Aurora | `mode_aurora` | 37 | 28 | 30 | Both sides drifted. |

### Effect IDs do not line up

This matters more than any individual effect. Numeric IDs match between MM and upstream only up to ID 76. After that:

| ID | MM | Upstream 16.0.1 |
| --- | --- | --- |
| 77 | Meteor Smooth | Copy Segment |
| 114 | (reserved, Candy Cane removed in 0.14) | Rotozoomer |
| 151 | `FX_MODE_2DFIRE2012` (defined but never registered) | PacMan |
| 187 to 197 | ARTI-FX, Party jerk, the four `_AR` audio effects, GEQ 3D, Paintbrush, Snow Fall | the first eleven 2D particle effects |
| 198 to 229 | the particle effects, shifted +11 | 187 to 218, plus Slow Transition at 219 |

MM's `MODE_COUNT` is 230, upstream's list ends at 219. MM has 224 `FX_MODE_*` defines, upstream 217. MM reserves 114, 169 to 171, 189 and 193 (commented-out defines for removed or planned effects).

Upstream-only effects with no MM equivalent: Copy Segment (`mode_copy_segment`), PacMan (`mode_pacman`), Slow Transition (`mode_slow_transition`), Rotozoomer (`mode_2Dplasmarotozoom`), and upstream's own `mode_rolling_balls` (MM's is named `rolling_balls` and differs).

For the ESPHome port, key effects off a stable string name, never off the WLED numeric ID, and keep a per-source mapping table.

## 5. Particle physics

**MM does not have its own particle system.** Both MM and upstream carry the same `wled00/FXparticleSystem.cpp` / `.h` by DedeHai (Damian Schneider), both headers say "Licensed under the EUPL v. 1.2 or later". MM's copy is a 2025 snapshot of upstream's engine; upstream 16.0.1's copy is a later evolution and has moved ahead.

| | MM (mdev) | Upstream 16.0.1 |
| --- | --- | --- |
| `FXparticleSystem.cpp` | 1,934 lines | 1,945 lines |
| `FXparticleSystem.h` | 400 lines | 420 lines |
| Copyright line | 2025 | 2024 |
| Render framebuffer | `CRGB *framebuffer` | `uint32_t *framebuffer` (comment says the compiler optimizes it better and FPS is more consistent) |
| Particle color type | `CRGB` | `CRGBW` |
| Per-particle size | not supported; no `perParticleSize` flag | `bool perParticleSize`, plus `renderLargeParticle()` in both 1D and 2D |
| Soft-edge rendering | none | `calculateEllipseBrightness()` inline helper for ellipse falloff |
| `updateFire` signature | `updateFire(intensity)` | `updateFire(intensity, renderonly)` for frame-skip and transition correctness |
| 2D collision | `collideParticles(p1, p2, dx, dy, collDistSq)` | adds `massratio1`, `massratio2` |
| 1D collision | takes particle refs plus flags plus `dx_abs` | takes indices plus `dx` |
| Free functions | exports `blur2D(CRGB*, ...)` | does not |
| `PS_P_MAXSPEED` | 120, comment does not mention collision rounding | 120, comment notes the overflow guard |

Practical consequences for a port:

* The 31 particle effects in MM (16 2D at IDs 198 to 212 and 228, 15 1D at IDs 213 to 227) are the same effects as upstream's 187 to 218, just at different IDs and against an older engine API.
* If you implement the engine once from upstream 16.0.1, MM's particle effects will mostly compile against it, but Pinball in particular differs (36 changed lines) and anything that relies on the CRGB-vs-CRGBW color width will need touching.
* MM gates them behind `WLED_DISABLE_PARTICLESYSTEM2D` / `WLED_DISABLE_PARTICLESYSTEM1D`, and hard-errors at compile time if both are enabled on ESP8266. The memory budget assumption is an ESP32.
* Conclusion: **port the upstream particle system, not MM's.** MM adds nothing here. The only MM-side particle content worth taking is the ID mapping.

## 6. MM engine features the effects depend on

These are the things that will bite during the ESPHome port, because MM effects call them directly.

### 6a. Extra `Segment` fields and layout

MM's `Segment` and upstream 16.0.1's `Segment` have diverged structurally, not just cosmetically.

MM has, and upstream does not:

* `CRGB *ledsrgb` plus `size_t ledsrgbSize` and `static CRGB *_globalLeds`. MM kept the per-segment `leds[]` array that upstream removed. Effects call `SEGMENT.setUpLeds()` to opt in, then read back with `getPixelColor()` losslessly. `setUpLeds` appears 76 times in MM's FX.cpp. Several MM-exclusive effects depend on it: GEQ 3D (2), Snow Fall (2), GEQ (2), Party jerk (2), Game Of Life (1), Paintbrush (1), Multi Comet audio (1).
* `void *jMap` plus `createjMap()` / `deletejMap()`, the MM "jMap" custom 1D-to-2D projection.
* `uint8_t lastBri` and `bool needsBlank`, MM's black-to-black transition optimisation and deferred-blank flag.
* `size_t _dataLen` and `size_t _usedSegmentData` (upstream uses `unsigned` / `uint16_t`), and `MAX_SEGMENT_OVERDATA` giving effects a 50 percent data overdraft budget beyond `MAX_SEGMENT_DATA`.
* Under `WLEDMM_FASTPATH`: cached `_2dWidth`, `_2dHeight`, `_virtuallength`, `_brightness`, `_isValid2D`, plus `_isSimpleSegment` / `_isSuperSimpleSegment` fast paths and a `setPixelColorXY_fast`. FX.cpp redefines `SEGMENT` to `(*strip._currentSeg)` under this flag.

Upstream has, and MM does not: `uint32_t *pixels` (the per-segment framebuffer that replaced `leds[]`), `uint8_t blendMode` and the sixteen segment blend modes, `_default_palette`, the static `_vLength` / `_vWidth` / `_vHeight` / `_currentColors` / `_currentPalette` caches, the `_clipStart` / `_clipStop` clipping rectangle, and `mutable` const-correctness throughout.

### 6b. 1D to 2D mapping enum: the numbers differ

```
MM:        M12_Pixels=0, M12_pBar=1, M12_pArc=2, M12_pCorner=3,
           M12_jMap=4, M12_sCircle=5, M12_sBlock=6, M12_sPinwheel=7
Upstream:  M12_Pixels=0, M12_pBar=1, M12_pArc=2, M12_pCorner=3,
           M12_sPinwheel=4
```

MM adds jMap, Circle and Block, and Pinwheel sits at 7 instead of 4. `map1D2D` is a 3-bit field in both, so MM has exactly filled it. Any `m12=` default in an MM metadata string means something different under upstream numbering. Metadata strings cannot be copied across forks without translating this.

### 6c. soundSim

Both have `SEGMENT.soundSim` and `simulateSound()`, but MM declares it as a 1-bit field (two simulation modes) and upstream as 2 bits (four). The `si=` default in MM metadata strings is therefore only meaningful 0 or 1.

### 6d. Audio data contract

MM's AudioReactive lives in a separate repo and publishes `um_data->u_size = 12`; upstream's in-tree `usermods/audioreactive/audio_reactive.cpp` publishes `u_size = 8`. Slots 0 to 7 match (volumeSmth float, volumeRaw uint16, fftResult uint8[16], samplePeak bool, FFT_MajorPeak float, my_magnitude float, maxVol uint8, binNum uint8). MM adds slots 8 to 11: `FFT_MajPeakSmth` (float, falls back to `FFT_MajorPeak` on ESP8266), `soundPressure` (float), `agcSensitivity` (float), `zeroCrossingCount` (uint16). `NUM_GEQ_CHANNELS` is 16 in both. MM effects reach the data through a `getAudioData()` helper that falls back to `simulateSound(SEGMENT.soundSim)`; a port needs the same graceful-degradation path or the audio effects will crash without a mic.

The AudioReactive repo registers zero effects, so nothing has to be inventoried there beyond the data contract. Note the build hack: `pio-scripts/patch_audioreactive.py` rewrites the fetched `library.json` to set `srcFilter: ["-<*>"]` so the library's `.cpp` is compiled as part of the main project rather than as a library. That is a PlatformIO workaround and does not translate to ESPHome.

### 6e. Float math

MM's FX.cpp has 182 `float`/`double` declarations versus upstream's 124, and MM uses the integer trig approximations (`sin_t`, `cos_t`, `atan2_t`) only once, against upstream's 14 uses. MM is markedly more float-heavy. The animartrix usermod goes further and `#define`s `floorf`, `sinf`, `cosf` and `tanf` to its own approximations for a claimed 40 percent speedup, with a comment warning that replacing `fmodf` the same way makes the radial oscillator effect "freak out" after a few hours. If you port animartrix-adjacent math, keep `fmodf` real.

MM also has a local `static long map2(long, ...)` helper used 26 times in FX.cpp, a signed-safe replacement for Arduino `map()`.

MM uses `mode_oops()` as a universal bail-out (179 references) for "wrong geometry" or "allocation failed". A port needs an equivalent early-return that does not leave stale pixels.

### 6f. PSRAM and memory

MM's `util.cpp` implements `d_malloc` / `d_calloc` / `d_realloc_malloc` / `d_free` with a three-tier ESP32 strategy: try RTC RAM for allocations under 1KB (or any size on an S2 without PSRAM), then internal DRAM with a `MIN_HEAP_SIZE` guard, then fall back to PSRAM via `heap_caps_malloc_prefer(MALLOC_CAP_SPIRAM)`. There is a separate `d_malloc_only` that refuses PSRAM. Effects do not call these directly (FX.cpp has zero direct PSRAM calls) but `SEGENV.allocateData()` and the particle system do, and the animartrix fork was forked specifically to add a PSRAM allocator. MM's boards list assumes PSRAM is common: roughly half the listed envs are `*_PSRAM_*`.

For an ESPHome component, this means the animartrix path effectively requires PSRAM, and the large particle and Game Of Life buffers want it.

### 6g. HUB75

MM ships a HUB75 bus type (`TYPE_HUB75MATRIX`, `WLED_ENABLE_HUB75MATRIX`, `ESP32-HUB75-MatrixPanel-I2S-DMA`, `HUB75_I2S_CFG activeMXconfig` in `bus_manager.h`), which upstream WLED does not have in core. Around a dozen of MM's PlatformIO envs are HUB75 builds, including a "MOONHUB HUB75 adapter board" env. The effect-visible part is `Bus::getPixelColorRestored()`, which HUB75 overrides because it has a lossless framebuffer. That is why so many MM effects call `setUpLeds()` and then read pixels back: on HUB75 the read is lossless, on WS2812 it is not. A port that has no equivalent lossless-readback bus needs to keep its own buffer for those effects or they will visibly degrade.

MM also raises `CONFIG_ASYNC_TCP_TASK_STACK_SIZE` to 9472 on ESP32 builds because audioreactive needs a bigger settings buffer, and defines `MIN_SHOW_DELAY` differently to support up to 250 FPS.

### 6h. `addEffect` is forgiving, so usermod IDs are not stable

MM's `WS2812FX::addEffect(id, fn, name)` at `FX.cpp:12151` treats the ID as a hint. If the slot is taken or out of range it falls back to the first free slot, and ID 255 means "first free slot" explicitly. The animartrix usermod asks for 203 to 254, which collide with core effects; whatever it actually gets depends on build flags and registration order. The games usermod asks for 255 three times. So **animartrix and games effect IDs are not reproducible and must not be hard-coded.**

## 7. License

### 7a. Repository

WLED-MM is **EUPL-1.2 or later**. `LICENSE` is the 291-line European Union Public Licence v1.2 text, and `readme.md` says so explicitly. Upstream WLED 16.0.1 is the same licence, with the copyright line "Copyright (c) 2016-present Christian Schwinne and individual WLED contributors, Licensed under the EUPL v. 1.2 or later". So the fork relationship itself is clean.

EUPL-1.2 is a copyleft licence. Its Appendix lists GPLv3 among the compatible licences, meaning EUPL-covered work may be relicensed under GPLv3 when combined with GPLv3 work. It is not a permissive licence, so an ESPHome component containing this code cannot be MIT or Apache.

### 7b. Per-file and per-effect headers, which are not uniform

The very first line of MM's `wled00/FX.cpp` is a warning:

```
/* Some portions of this code have other licenses, like GEQ 3D. Please review fully. */
```

That warning is accurate. The exceptions found:

| Location | Notice | Issue |
| --- | --- | --- |
| `FX.cpp:9048-9054` (GEQ 3D file block) | "@Copyright (c) 2024 Github MoonModules Commit Authors ... @license Licensed under the EUPL-1.2 or later" | |
| `FX.cpp:9063` (inside `mode_GEQLASER`) | "// Author: @TroyHacks  // @license GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007" | **Contradicts the block four lines above it.** The same effect is labelled EUPL-1.2 and GPLv3. This needs resolving with the authors before shipping GEQ 3D. |
| `FX.cpp:9195-9211` (Paintbrush file block) | Full GPLv3 grant: "WLED-MM is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License ... version 3 ... or (at your option) any later version." | Unambiguously GPLv3, not EUPL. |
| `FX.cpp:9219` (inside `mode_2DPaintbrush`) | "// Author: @TroyHacks  // @license GNU GENERAL PUBLIC LICENSE Version 3" | Consistent with its block. |
| `FXparticleSystem.cpp` and `.h` | "Copyright (c) 2025 Damian Schneider, Licensed under the EUPL v. 1.2 or later" | Clean. |
| `MoonModules/WLED-AudioReactive-Usermod` | EUPL-1.2 (`LICENSE` at repo root) | Clean. |

### 7c. ANIMartRIX: the blocker

`platformio.ini:1224` is explicit:

```
animartrix_build_flags = -D USERMOD_ANIMARTRIX ;; WLEDMM usermod: CC BY-NC 3.0 licensed effects by Stefan Petrick
```

and `usermod_v2_animartrix.h:69` makes the compiler say so:

```
#warning WLEDMM usermod: CC BY-NC 3.0 licensed effects by Stefan Petrick, include this usermod only if you accept the terms!
```

and the usermod's own settings UI prints "Animartrix requires the Creative Commons Attribution License CC BY-NC 3.0" (`usermod_v2_animartrix.h:525`).

**CC BY-NC 3.0 is a non-commercial licence. It is not an open source licence, and it is not compatible with GPLv3 or with EUPL-1.2.** The NC restriction cannot be satisfied by a licence that grants unrestricted commercial redistribution, so these 51 effects cannot be combined into a GPLv3 or EUPL-1.2 work and redistributed. MM handles this by making animartrix an opt-in build flag with a compiler warning and a UI acknowledgement, and by pulling the library from a separate repo rather than vendoring it.

Practical read for the ESPHome port:

1. **Do not port the 51 animartrix effects.** That is 51 of the 64 MM-exclusive effects, so the MM-exclusive haul shrinks to 13 once animartrix is excluded, and to 9 core entries once ARTI-FX and the 3 games are excluded too.
2. If they are wanted later, the only defensible pattern is MM's: a separate, opt-in, user-fetched module that is never distributed inside the main component, with the CC BY-NC notice surfaced. Even that is legally awkward for anything a commercial vendor ships.
3. **GEQ 3D and Paintbrush are GPLv3 (or contradictory, for GEQ 3D).** Usable only if the ESPHome component is itself GPLv3 or later. If the component is meant to be permissively licensed, both must be dropped or the authors asked to relicense. GEQ 3D's contradiction should be raised regardless, because right now nobody can say which licence governs it.
4. **Everything else in MM's FX.cpp is EUPL-1.2 or later**, which is fine for a GPLv3 component (via the EUPL compatibility appendix) and fine for an EUPL component. Verify the licence ESPHome expects for an external component before committing to a direction.
5. The FX.cpp banner says "review fully". A per-effect header sweep of any effect actually selected for porting is the minimum due diligence; the four notices above are what a grep for licence keywords found, not a guarantee that nothing else is buried in a longer comment.

## 8. Surprises worth flagging

1. **MM is behind upstream on the particle system, not ahead.** The expectation going in was that MM might have its own engine. It does not, and its copy is the older one.
2. **Upstream 16.0.1 changed every effect signature to `void`.** MM is still on `uint16_t` returning `FRAMETIME`. Any shared C++ effect layer has to pick one calling convention and adapt the other side; this is 211 functions on the MM side.
3. **MM effect IDs diverge from ID 77 onwards** and the whole particle block is offset by 11. Nothing can key on the numeric ID.
4. **MM's `m12=` metadata defaults are not portable** because Pinwheel moved from 4 to 7.
5. **The MM-exclusive haul is much smaller than the headline number.** 275 total effects, 64 not in upstream, but 51 of those are the licence-blocked animartrix wrappers, 1 is a scripting interpreter, 3 are IMU games. Nine core entries remain, and of those one (Meteor Smooth) is an effect upstream deleted rather than an MM invention and three are one-line audio wrappers around shared cores. So eight genuinely new MM effects, of which two are licence-encumbered.
6. **GEQ 3D carries two contradictory licences four lines apart.**
7. **`setUpLeds()` is load-bearing.** MM's decision to keep the `leds[]` array is why several of its best effects read pixels back. A port onto an architecture without lossless readback will not reproduce them faithfully.
8. **`addEffect` silently relocates colliding IDs**, so the animartrix and games IDs listed below are requests, not facts.

## Appendix A. Full core effect inventory (FX.cpp, 220 effects)

| ID | Name | Function | Category | Metadata | ~Lines |
| --- | --- | --- | --- | --- | --- |
| 1 | Blink | `mode_blink` | 0D, 1D | `Blink@!,Duty cycle;!,!;!;01` | 3 |
| 2 | Breathe | `mode_breath` | 0D, 1D | `Breathe@!;!,!;!;01` | 16 |
| 3 | Wipe | `mode_color_wipe` | 1D | `Wipe@!,!;!,!;!` | 3 |
| 4 | Wipe Random | `mode_color_wipe_random` | 1D | `Wipe Random@!;;!` | 3 |
| 5 | Random Colors | `mode_random_color` | 0D, 1D | `Random Colors@!,Fade time;;!;01` | 26 |
| 6 | Sweep | `mode_color_sweep` | 1D | `Sweep@!,!;!,!;!` | 3 |
| 7 | Dynamic | `mode_dynamic` | 1D | `Dynamic@!,!,,,,Smooth;;!` | 30 |
| 8 | Colorloop | `mode_rainbow` | 0D, 1D | `Colorloop@!,Saturation;;!;01` | 12 |
| 9 | Rainbow | `mode_rainbow_cycle` | 1D | `Rainbow@!,Size;;!` | 12 |
| 10 | Scan | `mode_scan` | 1D | `Scan@!,# of dots,,,,,Overlay;!,!,!;!` | 3 |
| 11 | Scan Dual | `mode_dual_scan` | 1D | `Scan Dual@!,# of dots,,,,,Overlay;!,!,!;!` | 3 |
| 12 | Fade | `mode_fade` | 0D, 1D | `Fade@!;!,!;!;01` | 10 |
| 13 | Theater | `mode_theater_chase` | 1D | `Theater@!,Gap size;!,!;!` | 3 |
| 14 | Theater Rainbow | `mode_theater_chase_rainbow` | 1D | `Theater Rainbow@!,Gap size;,!;!` | 3 |
| 15 | Running | `mode_running_lights` | 1D | `Running@!,Wave width;!,!;!` | 3 |
| 16 | Saw | `mode_saw` | 1D | `Saw@!,Width;!,!;!` | 3 |
| 17 | Twinkle | `mode_twinkle` | 1D | `Twinkle@!,!;!,!;!;;m12=0` | 29 |
| 18 | Dissolve | `mode_dissolve` | 1D | `Dissolve@Repeat speed,Dissolve speed,,,,Random;!,!;!` | 3 |
| 19 | Dissolve Rnd | `mode_dissolve_random` | 1D | `Dissolve Rnd@Repeat speed,Dissolve speed;,!;!` | 3 |
| 20 | Sparkle | `mode_sparkle` | 1D | `Sparkle@!,,,,,,Overlay;!,!;!;;m12=0` | 15 |
| 21 | Sparkle Dark | `mode_flash_sparkle` | 1D | `Sparkle Dark@!,!,,,,,Overlay;Bg,Fx;!;;m12=0` | 14 |
| 22 | Sparkle+ | `mode_hyper_sparkle` | 1D | `Sparkle+@!,!,,,,,Overlay;Bg,Fx;!;;m12=0` | 16 |
| 23 | Strobe | `mode_strobe` | 0D, 1D | `Strobe@!;!,!;!;01` | 3 |
| 24 | Strobe Rainbow | `mode_strobe_rainbow` | 0D, 1D | `Strobe Rainbow@!;,!;!;01` | 3 |
| 25 | Strobe Mega | `mode_multi_strobe` | 0D, 1D | `Strobe Mega@!,!;!,!;!;01` | 24 |
| 26 | Blink Rainbow | `mode_blink_rainbow` | 0D, 1D | `Blink Rainbow@Frequency,Blink duration;!,!;!;01` | 3 |
| 27 | Android | `mode_android` | 1D | `Android@!,Width;!,!;!;;m12=1` | 37 |
| 28 | Chase | `mode_chase_color` | 1D | `Chase@!,Width;!,!,!;!` | 3 |
| 29 | Chase Random | `mode_chase_random` | 1D | `Chase Random@!,Width;!,,!;!` | 3 |
| 30 | Chase Rainbow | `mode_chase_rainbow` | 1D | `Chase Rainbow@!,Width;!,!;!` | 8 |
| 31 | Chase Flash | `mode_chase_flash` | 1D | `Chase Flash@!;Bg,Fx;!` | 24 |
| 32 | Chase Flash Rnd | `mode_chase_flash_random` | 1D | `Chase Flash Rnd@!;!,!;!` | 30 |
| 33 | Rainbow Runner | `mode_chase_rainbow_white` | 1D | `Rainbow Runner@!,Size;Bg;!` | 8 |
| 34 | Colorful | `mode_colorful` | 1D | `Colorful@!,Saturation;1,2,3;!` | 39 |
| 35 | Traffic Light | `mode_traffic_light` | 1D | `Traffic Light@!,US style;,!;!` | 26 |
| 36 | Sweep Random | `mode_color_sweep_random` | 1D | `Sweep Random@!;;!` | 3 |
| 37 | Chase 2 | `mode_running_color` | 1D | `Chase 2@!,Width;!,!;!` | 3 |
| 38 | Aurora | `mode_aurora` | 1D | `Aurora@!,!;1,2,3;!;;sx=24,pal=50` | 60 |
| 39 | Stream ☾ | `mode_running_random` | 1D | `Stream ☾@!,Zone size;;!` | 31 |
| 40 | Scanner | `mode_larson_scanner` | 1D | `Scanner@!,Fade rate;!,!;!;;m12=0` | 3 |
| 41 | Lighthouse | `mode_comet` | 1D | `Lighthouse@!,Fade rate;!,!;!` | 26 |
| 42 | Fireworks | `mode_fireworks` | 1D, 2D | `Fireworks@,Frequency;!,!;!;12;ix=192,pal=11` | 1 |
| 43 | Rain | `mode_rain` | 1D, 2D | `Rain@!,Spawning rate;!,!;!;12;ix=128,pal=0` | 35 |
| 44 | Tetrix | `mode_tetrix` | 1D | `Tetrix@!,Width,,,,One color;!,!;!;1.5d;sx=0,ix=0,pal=11,m12=1` | 74 |
| 45 | Fire Flicker | `mode_fire_flicker` | 0D, 1D | `Fire Flicker@!,!;!;!;01;pal=0` | 23 |
| 46 | Gradient | `mode_gradient` | 1D | `Gradient@!,Spread;!,!;!;;ix=16` | 3 |
| 47 | Loading | `mode_loading` | 1D | `Loading@!,Fade;!,!;!;;ix=16` | 3 |
| 48 | Rolling Balls | `rolling_balls` | 1D | `Rolling Balls@!,# of balls,,,,Collisions,Overlay;!,!,!;!;1;m12=1` | 82 |
| 49 | Fairy | `mode_fairy` | 1D | `Fairy@!,# of flashers;!,!;!` | 71 |
| 50 | Two Dots | `mode_two_dots` | 1D | `Two Dots@!,Dot size,,,,,Overlay;1,2,Bg;!` | 6 |
| 51 | Fairytwinkle | `mode_fairytwinkle` | 1D | `Fairytwinkle@!,!;!,!;!;;m12=0` | 43 |
| 52 | Running Dual | `mode_running_dual` | 1D | `Running Dual@!,Wave width;L,!,R;!` | 3 |
| 53 | Image | `mode_image` | 1D, 2D | `Image@!,Blur,;;;12;sx=128,ix=0` | 14 |
| 54 | Chase 3 | `mode_tricolor_chase` | 1D | `Chase 3@!,Size;1,2,3;!` | 3 |
| 55 | Tri Wipe | `mode_tricolor_wipe` | 1D | `Tri Wipe@!;1,2,3;!` | 34 |
| 56 | Tri Fade | `mode_tricolor_fade` | 0D, 1D | `Tri Fade@!;1,2,3;!;01` | 36 |
| 57 | Lightning | `mode_lightning` | 1D | `Lightning@!,!,,,,,Overlay;!,!;!` | 40 |
| 58 | ICU | `mode_icu` | 1D | `ICU@!,!,,,,,Overlay;!,!;!` | 35 |
| 59 | Multi Comet | `mode_multi_comet` | 1D | `Multi Comet` | 32 |
| 60 | Scanner Dual | `mode_dual_larson_scanner` | 1D | `Scanner Dual@!,Fade rate;!,!,!;!;;m12=0` | 3 |
| 61 | Stream 2 ☾ | `mode_random_chase` | 1D | `Stream 2 ☾@!;;` | 28 |
| 62 | Oscillate | `mode_oscillate` | 1D | `Oscillate` | 48 |
| 63 | Pride 2015 | `mode_pride_2015` | 1D | `Pride 2015@!;;` | 40 |
| 64 | Juggle | `mode_juggle` | 1D | `Juggle@!,Trail;;!;;sx=64,ix=128` | 20 |
| 65 | Palette | `mode_palette` | 1D | `Palette@Cycle speed;;!;;c3=0,o2=0` | 16 |
| 66 | Fire 2012 | `mode_fire_2012` | 1D | `Fire 2012@Cooling,Spark rate,,2D Blur,Boost;;!;1.5d;sx=64,ix=160,c2=128,m12=1` | 62 |
| 67 | Colorwaves | `mode_colorwaves` | 1D | `Colorwaves@!,Hue;!;!` | 45 |
| 68 | Bpm | `mode_bpm` | 1D | `Bpm@!;!;!;;sx=64` | 12 |
| 69 | Fill Noise | `mode_fillnoise8` | 1D | `Fill Noise@!;!;!` | 13 |
| 70 | Noise 1 | `mode_noise16_1` | 1D | `Noise 1@!;!;!` | 21 |
| 71 | Noise 2 | `mode_noise16_2` | 1D | `Noise 2@!;!;!` | 18 |
| 72 | Noise 3 | `mode_noise16_3` | 1D | `Noise 3@!;!;!` | 21 |
| 73 | Noise 4 | `mode_noise16_4` | 1D | `Noise 4@!;!;!` | 11 |
| 74 | Colortwinkles | `mode_colortwinkle` | 1D | `Colortwinkles@Fade speed,Spawn speed;;!;;m12=0` | 55 |
| 75 | Lake | `mode_lake` | 1D | `Lake@!;Fx;!` | 18 |
| 76 | Meteor | `mode_meteor` | 1D | `Meteor@!,Trail,,,,Gradient,,Smooth;;!;1` | 3 |
| 77 | Meteor Smooth | `mode_meteor_smooth` | 1D | `Meteor Smooth@!,Trail,,,,Gradient;;!;1` | 3 |
| 78 | Railway | `mode_railway` | 1D | `Railway@!,Smoothness;1,2;!` | 28 |
| 79 | Ripple | `mode_ripple` | 1D, 2D | `Ripple@!,Wave #,,,,,Overlay;,!;!;12` | 6 |
| 80 | Twinklefox | `mode_twinklefox` | 1D | `Twinklefox@!,Twinkle rate,,,,Cool;!,!;!` | 4 |
| 81 | Twinklecat | `mode_twinklecat` | 1D | `Twinklecat@!,Twinkle rate,,,,Cool,Reverse;!,!;!` | 4 |
| 82 | Halloween Eyes | `mode_halloween_eyes` | 1D, 2D | `Halloween Eyes@Duration,Eye fade time,,,,,Overlay;!,!;!;12` | 59 |
| 83 | Solid Pattern | `mode_static_pattern` | 1D | `Solid Pattern@Fg size,Bg size;Fg,!;!;;pal=0` | 18 |
| 84 | Solid Pattern Tri | `mode_tri_static_pattern` | 1D | `Solid Pattern Tri@,Size;1,2,3;;;pal=0` | 23 |
| 85 | Spots | `mode_spots` | 1D | `Spots@Spread,Width,,,,,Overlay;!,!;!` | 4 |
| 86 | Spots Fade | `mode_spots_fade` | 1D | `Spots Fade@Speed,Width,,,,,Overlay;!,!;!` | 7 |
| 87 | Glitter | `mode_glitter` | 1D | `Glitter@!,!,,,,,Overlay;1,2,Glitter color;!;;pal=0,m12=0` | 6 |
| 88 | Candle | `mode_candle` | 0D, 1D | `Candle@!,!;!,!;!;01;sx=96,ix=224,pal=0` | 4 |
| 89 | Fireworks Starburst | `mode_starburst` | 1D | `Fireworks Starburst@Chance,Fragments,,,,,Overlay;,!;!;;pal=11,m12=0` | 1 |
| 90 | Fireworks 1D | `mode_exploding_fireworks` | 1D, 2D | `Fireworks 1D@Gravity,Firing side;!,!;!;12;pal=11,ix=128` | 135 |
| 91 | Bouncing Balls | `mode_bouncing_balls` | 1D | `Bouncing Balls@Gravity,# of balls,,,,,Overlay;!,!,!;!;1.5d;m12=1` | 67 |
| 92 | Sinelon | `mode_sinelon` | 1D | `Sinelon@!,Trail;!,!,!;!` | 3 |
| 93 | Sinelon Dual | `mode_sinelon_dual` | 1D | `Sinelon Dual@!,Trail;!,!,!;!` | 3 |
| 94 | Sinelon Rainbow | `mode_sinelon_rainbow` | 1D | `Sinelon Rainbow@!,Trail;,,!;!` | 3 |
| 95 | Popcorn ☾ | `mode_popcorn` | 1D | `Popcorn ☾@!,!,,,,,Overlay;!,!,!;!;1.5d;m12=1` | 1 |
| 96 | Drip ☾ | `mode_drip` | 1D | `Drip ☾@Gravity,# of drips,Fall ratio,,,,Overlay;!,!;!;1.5d;c1=127,m12=1` | 103 |
| 97 | Plasma | `mode_plasma` | 1D | `Plasma@Phase,!;!;!` | 19 |
| 98 | Percent | `mode_percent` | 1D | `Percent@!,% of fill,,,,One color;!,!;!` | 46 |
| 99 | Ripple Rainbow | `mode_ripple_rainbow` | 1D, 2D | `Ripple Rainbow@!,Wave #;;!;12` | 16 |
| 100 | Heartbeat | `mode_heartbeat` | 0D, 1D | `Heartbeat@!,!;!,!;!;01;m12=1` | 26 |
| 101 | Pacifica | `mode_pacifica` | 1D | `Pacifica@!,Angle;;!;;pal=51` | 74 |
| 102 | Candle Multi | `mode_candle_multi` | 1D | `Candle Multi@!,!;!,!;!;;sx=96,ix=224,pal=0` | 4 |
| 103 | Solid Glitter | `mode_solid_glitter` | 1D | `Solid Glitter@,!;Bg,,Glitter color;;;m12=0` | 6 |
| 104 | Sunrise | `mode_sunrise` | 1D | `Sunrise@Time [min],Width;;!;;sx=60` | 48 |
| 105 | Phased | `mode_phased` | 1D | `Phased@!,!;!,!;!` | 3 |
| 106 | Twinkleup | `mode_twinkleup` | 1D | `Twinkleup@!,Intensity;!,!;!;;m12=0` | 14 |
| 107 | Noise Pal | `mode_noisepal` | 1D | `Noise Pal@!,Scale;;!` | 35 |
| 108 | Sine | `mode_sinewave` | 1D | `Sine` | 16 |
| 109 | Phased Noise | `mode_phased_noise` | 1D | `Phased Noise@!,!;!,!;!` | 3 |
| 110 | Flow | `mode_flow` | 1D | `Flow@!,Zones;;!;;m12=1` | 32 |
| 111 | Chunchun | `mode_chunchun` | 1D | `Chunchun@!,Gap size;!,!;!` | 20 |
| 112 | Dancing Shadows | `mode_dancing_shadows` | 1D | `Dancing Shadows@!,# of shadows;!;!` | 116 |
| 113 | Washing Machine | `mode_washing_machine` | 1D | `Washing Machine@!,!;;!` | 12 |
| 115 | Blends | `mode_blends` | 1D | `Blends@Shift speed,Blend speed;;!` | 21 |
| 116 | TV Simulator | `mode_tv_simulator` | 0D, 1D | `TV Simulator@!,!;;!;01` | 101 |
| 117 | Dynamic Smooth | `mode_dynamic_smooth` | 1D | `Dynamic Smooth@!,!;;!` | 7 |
| 118 | Spaceships | `mode_2Dspaceships` | 2D | `Spaceships@!,Blur;;!;2` | 40 |
| 119 | Crazy Bees | `mode_2Dcrazybees` | 2D | `Crazy Bees@!,Blur;;;2` | 67 |
| 120 | Ghost Rider | `mode_2Dghostrider` | 2D | `Ghost Rider@Fade rate,Blur;;!;2` | 82 |
| 121 | Blobs | `mode_2Dfloatingblobs` | 2D | `Blobs@!,# blobs,Blur;!;!;2;c1=8` | 99 |
| 122 | Scrolling Text | `mode_2Dscrollingtext` | 2D | `Scrolling Text@!,Y Offset,Trail,Font size,,Gradient,Overlay,Soft;!,!,Gradient;!;2;ix=128,c1=0,rev=0,mi=0,rY=0,mY=0` | 129 |
| 123 | Drift Rose | `mode_2Ddriftrose` | 2D | `Drift Rose@Fade,Blur,,,,,,Full Expand ☾;;;2` | 32 |
| 124 | Distortion Waves | `mode_2Ddistortionwaves` | 2D | `Distortion Waves@!,Scale,,,,Fill,Zoom,Alt;;!;2;pal=0` | 79 |
| 125 | Soap | `mode_2Dsoap` | 2D | `Soap@!,Smoothness,Density;;!;2;pal=11` | 59 |
| 126 | Octopus | `mode_2Doctopus` | 2D | `Octopus@!,,Offset X,Offset Y,Legs, SuperSync,,RadialWave ☾;;!;2;` | 80 |
| 127 | Waving Cell | `mode_2Dwavingcell` | 2D | `Waving Cell@!,Blur,Amplitude 1,Amplitude 2,Amplitude 3,,Flow;;!;2;ix=0` | 20 |
| 128 | Pixels | `mode_pixels` | 1D, audio volume | `Pixels@Fade rate,# of pixels;!,!;!;1v;m12=0,si=0` | 20 |
| 129 | Pixelwave | `mode_pixelwave` | 0D, 1D, audio volume | `Pixelwave@!,Sensitivity;!,!;!;01v;ix=64,m12=2,si=0` | 26 |
| 130 | Juggles | `mode_juggles` | 0D, 1D, audio volume | `Juggles@!,# of balls;!,!;!;01v;m12=0,si=0` | 15 |
| 131 | Matripix ☾ | `mode_matripix` | 1D, audio volume | `Matripix ☾@!,Brightness,,,,Frequency Colors,Sound Pressure;!,!;!;1v;ix=96,m12=2,si=1` | 46 |
| 132 | Gravimeter ☾ | `mode_gravimeter` | 1D, audio volume | `Gravimeter ☾@Rate of fall,Sensitivity,,,,Invert Palette,Sound Pressure,AGC debug;!,!;!;1v;ix=128,m12=2,si=0` | 72 |
| 133 | Plasmoid | `mode_plasmoid` | 0D, 1D, audio volume | `Plasmoid@Phase,# of pixels;!,!;!;01v;sx=128,ix=80,pal=8,m12=0,si=0` | 31 |
| 134 | Puddles | `mode_puddles` | 1D, audio volume | `Puddles@Fade rate,Puddle size;!,!;!;1v;m12=0,si=0` | 25 |
| 135 | Midnoise | `mode_midnoise` | 1D, audio volume | `Midnoise@Fade rate,Max. length;!,!;!;1v;sx=206,ix=128,m12=1,si=0` | 29 |
| 136 | Noisemeter | `mode_noisemeter` | 1D, audio volume | `Noisemeter@Fade rate,Width;!,!;!;1v;;sx=248,ix=128,m12=2,si=0` | 26 |
| 137 | Freqwave | `mode_freqwave` | 0D, 1D, audio FFT | `Freqwave@Speed,Sound effect,Low bin,High bin,Pre-amp,Musical Scale ☾;;;01f;c1=18,c2=48,m12=2,si=0` | 60 |
| 138 | Freqmatrix | `mode_freqmatrix` | 0D, 1D, audio FFT | `Freqmatrix@Speed,Sound effect,Low bin,High bin,Sensitivity;;;01f;c1=18,c2=48,c3=6,m12=3,si=0` | 46 |
| 139 | GEQ ☾ | `mode_2DGEQ` | 1D, 2D, audio FFT | `GEQ ☾@Fade speed,Ripple decay,# of bands,,,Color bars,Smooth bars ☾;!,,Peaks;!;12f;c1=255,c2=64,pal=11,si=0` | 103 |
| 140 | Waterfall | `mode_waterfall` | 0D, 1D, audio FFT | `Waterfall@!,Adjust color,Select bin,Volume (min);!,!;!;01f;c1=8,c2=48,m12=2,si=0` | 47 |
| 141 | Freqpixels | `mode_freqpixels` | 1D, audio FFT | `Freqpixels@Fade rate,Starting color and # of pixels;;;1f;sx=204,m12=0,si=0` | 24 |
| 143 | Noisefire | `mode_noisefire` | 0D, 1D, audio volume | `Noisefire@!,!;;;01v;m12=2,si=0` | 22 |
| 144 | Puddlepeak | `mode_puddlepeak` | 1D, audio volume | `Puddlepeak@Fade rate,Puddle size,Select bin,Volume (min);!,!;!;1v;c1=8,c2=48,m12=0,si=0` | 37 |
| 145 | Noisemove | `mode_noisemove` | 0D, 1D, audio FFT | `Noisemove@Speed of perlin movement,Fade rate;!,!;!;01f;m12=0,si=0` | 23 |
| 146 | Noise2D | `mode_2Dnoise` | 2D | `Noise2D@!,Scale;;!;2` | 17 |
| 147 | Perlin Move | `mode_perlinmove` | 1D | `Perlin Move@!,# of pixels,Fade rate;!,!;!` | 12 |
| 148 | Ripple Peak | `mode_ripplepeak` | 1D, audio volume | `Ripple Peak@Fade rate,Max # of ripples,Select bin,Volume (min);!,!;!;1v;c1=8,c2=48,m12=0,si=0` | 76 |
| 149 | Firenoise | `mode_2Dfirenoise` | 2D | `Firenoise@X scale,Y scale,,,,Palette;;!;2;pal=0` | 29 |
| 150 | Squared Swirl | `mode_2Dsquaredswirl` | 2D | `Squared Swirl@,,,,Blur;;!;2` | 35 |
| 152 | DNA | `mode_2Ddna` | 2D | `DNA@Scroll speed,Blur,Phases;;!;2` | 39 |
| 153 | Matrix | `mode_2Dmatrix` | 2D | `Matrix@!,Spawning rate,Trail,,,Custom color;Spawn,Trail;;2` | 70 |
| 154 | Metaballs | `mode_2Dmetaballs` | 2D | `Metaballs@!;;!;2` | 53 |
| 155 | Freqmap | `mode_freqmap` | 1D, audio FFT | `Freqmap@Fade rate,Starting color,,,,Smooth mover ☾;!,!;!;1f;sx=192,m12=0,si=0,o1=1` | 37 |
| 156 | Gravcenter | `mode_gravcenter` | 1D, audio volume | `Gravcenter@Rate of fall,Sensitivity;!,!;!;1v;ix=128,m12=2,si=0` | 42 |
| 157 | Gravcentric | `mode_gravcentric` | 1D, audio volume | `Gravcentric@Rate of fall,Sensitivity;!,!;!;1v;ix=128,m12=3,si=0` | 45 |
| 158 | Gravfreq ☾ | `mode_gravfreq` | 1D, audio FFT | `Gravfreq ☾@Rate of fall,Sensitivity;!,!;!;1f;ix=128,m12=0,si=0` | 48 |
| 159 | DJ Light | `mode_DJLight` | 0D, 1D, audio FFT | `DJ Light@Speed,,,,,Candy Factory;;;01f;m12=2,si=0` | 60 |
| 160 | Funky Plank | `mode_2DFunkyPlank` | 2D, audio FFT | `Funky Plank@Scroll speed,,# of bands;;;2f;si=0` | 49 |
| 161 | Shimmer | `mode_shimmer` | 1D | `Shimmer@Speed,Interval,Size,Granular,Flow,Zebra,Reverse,Sporadic;Fx,Bg,Cx;!;1;pal=15,sx=220,ix=10,c2=0,c3=0` | 66 |
| 162 | Pulser | `mode_2DPulser` | 2D | `Pulser@!,Blur;;!;2` | 22 |
| 163 | Blurz Plus ☾ | `mode_blurz` | 0D, 1D, audio FFT | `Blurz Plus ☾@Fade rate,Blur,,,,FreqMap ☾,GEQ Scanner ☾,;!,Color mix;!;01f;sx=48,ix=127,m12=7,si=0` | 62 |
| 164 | Drift | `mode_2DDrift` | 2D | `Drift@Rotation speed,Blur,,,,Twin,Smear;;!;2;ix=0` | 49 |
| 165 | Waverly ☾ | `mode_2DWaverly` | 2D, audio volume | `Waverly ☾@Fade Rate,Amplification,,,,No Clouds,Sound Pressure,AGC debug;;!;2v;ix=64,si=0` | 42 |
| 166 | Sun Radiation | `mode_2DSunradiation` | 2D | `Sun Radiation@Variance,Brightness;;;2` | 48 |
| 167 | Colored Bursts | `mode_2DColoredBursts` | 2D | `Colored Bursts@Speed,# of lines,,,Blur,Gradient,,Dots;;!;2;c3=16` | 48 |
| 168 | Julia | `mode_2DJulia` | 2D | `Julia@,Max iterations per pixel,X center,Y center,Area size,Soft Blur,Strong Blur,Show Center;!;!;2;ix=24,c1=128,c2=128,c3=16` | 118 |
| 172 | Game Of Life | `mode_2Dgameoflife` | 2D | `Game Of Life@!,Color Mutation ☾,Blur ☾,,,All Colors ☾,Overlay BG ☾,Wrap ☾;!,!;!;2;sx=56,ix=2,c1=128,o1=0,o2=0,o3=1` | 203 |
| 173 | Tartan | `mode_2Dtartan` | 2D | `Tartan@X scale,Y scale,,,Sharpness;;!;2` | 34 |
| 174 | Polar Lights | `mode_2DPolarLights` | 2D | `Polar Lights@!,Scale,,,,Use Palette,SuperSync, Blur;;!;2` | 70 |
| 175 | Swirl | `mode_2DSwirl` | 2D, audio volume | `Swirl@!,Sensitivity,Blur;,Bg Swirl;!;2v;ix=64,si=0` | 36 |
| 176 | Lissajous ☾ | `mode_2DLissajous` | 2D | `Lissajous ☾@X frequency,Fade rate,,,Speed,,,☾ Smooth Style;!;!;2;sx=64,c3=15` | 36 |
| 177 | Frizzles | `mode_2DFrizzles` | 2D | `Frizzles@X frequency,Y frequency,Blur;;!;2` | 21 |
| 178 | Plasma Ball | `mode_2DPlasmaball` | 2D | `Plasma Ball@Speed,,Fade,Blur;;!;2` | 37 |
| 179 | Flow Stripe | `mode_FlowStripe` | 1D | `Flow Stripe@Hue speed,Effect speed;;!;pal=11` | 16 |
| 180 | Hiphotic | `mode_2DHiphotic` | 2D | `Hiphotic@X scale,Y scale,,,Speed;!;!;2` | 15 |
| 181 | Sindots | `mode_2DSindots` | 2D | `Sindots@!,Dot distance,Fade rate,Blur;;!;2` | 24 |
| 182 | DNA Spiral | `mode_2DDNASpiral` | 2D | `DNA Spiral@Scroll speed,Y frequency;;!;2` | 42 |
| 183 | Black Hole | `mode_2DBlackHole` | 2D | `Black Hole@Fade rate,Outer Y freq.,Outer X freq.,Inner X freq.,Inner Y freq.;;;2` | 35 |
| 184 | Wavesins | `mode_wavesins` | 1D | `Wavesins@!,Brightness variation,Starting color,Range of colors,Color variation;!;!` | 11 |
| 185 | Rocktaves | `mode_rocktaves` | 0D, 1D, audio FFT | `Rocktaves@;!,!;!;01f;m12=1,si=0` | 34 |
| 186 | Akemi | `mode_2DAkemi` | 2D, audio FFT | `Akemi@Color speed,Dance;Head palette,Arms & Legs,Eyes & Mouth;Face palette;2f;si=0` | 3 |
| 188 | Party jerk | `mode_partyjerk` | 1D, audio volume | `Party jerk@Effect speed,Sensitivity,Color change speed,Effect speed active multiplier;!,!;!;1v;c1=8,c2=48,m12=0,si=0` | 52 |
| 190 | Popcorn audio ☾ | `mode_popcorn_audio` | 1D, audio volume | `Popcorn audio ☾@!,!,,,,,Overlay;!,!,!;!;1v,1.5d;m12=1` | 1 |
| 191 | Multi Comet audio ☾ | `mode_multi_comet_ar` | 1D, audio volume | `Multi Comet audio ☾@Speed,Tail Length;!,!;!;1v;sx=160,ix=32,m12=7,si=1` | 53 |
| 192 | Fw Starburst audio ☾ | `mode_starburst_audio` | 1D, audio volume | `Fw Starburst audio ☾@Chance,Fragments,,,,,Overlay;,!;!;1v;pal=11,m12=0` | 1 |
| 194 | Fireworks audio ☾ | `mode_fireworks_audio` | 1D, 2D, audio volume | `Fireworks audio ☾@,Frequency;!,!;!;1v,12;ix=192,pal=11` | 1 |
| 195 | GEQ 3D ☾ | `mode_GEQLASER` | 2D, audio FFT | `GEQ 3D ☾@Speed,Front Fill,Horizon,Depth,Num Bands,Borders,Soft,;!,,Peaks;!;2f;sx=255,ix=228,c1=255,c2=255,c3=15,pal=11` | 133 |
| 196 | Paintbrush ☾ | `mode_2DPaintbrush` | 2D, audio FFT | `Paintbrush ☾@Oscillator Offset,# of lines,Fade Rate,,Min Length,Color Chaos,Anti-aliasing,Phase Chaos;!,,Peaks;!;2f;sx=160,ix=255,c1=80,c2=255,c3=0,pal=72,o1=0,o2=1,o3=0` | 56 |
| 197 | Snow Fall ☾ | `mode_2DSnowFall` | 2D | `Snow Fall ☾@!,Spawn Rate,Despawn Rate,Blur,Sway Chance,Use Palette,Inverted Overlay,Prevent Overflow,;!,!;!;2;sx=128,ix=16,c1=17,c2=0,c3=0,o1=0,o2=0,o3=1` | 108 |
| 198 | PS Volcano | `mode_particlevolcano` | 2D, particle | `PS Volcano@Speed,Intensity,Move,Bounce,Spread,AgeColor,Walls,Collide;;!;2;pal=35,sx=208,ix=190,c1=0,c2=190,c3=16,o1=1` | 64 |
| 199 | PS Fire | `mode_particlefire` | 2D, particle | `PS Fire@Speed,Intensity,Flame Height,Wind,Spread,Smooth,Cylinder,Turbulence;;!;2;pal=35,sx=110,c1=110,c2=50,c3=31,o1=1` | 99 |
| 200 | PS Fireworks | `mode_particlefireworks` | 2D, particle | `PS Fireworks@Launches,Explosion Size,Fuse,Blur,Gravity,Cylinder,Ground,Fast;;!;2;pal=11,ix=200,sx=180,c1=196,c2=220,c3=10,o3=1` | 134 |
| 201 | PS Vortex | `mode_particlevortex` | 2D, particle | `PS Vortex@Rotation Speed,Particle Speed,Arms,Flip,Nozzle,Smear,Direction,Random Flip;;!;2;pal=27,c1=200,c2=0,c3=0` | 105 |
| 202 | PS Fuzzy Noise | `mode_particleperlin` | 2D, particle | `PS Fuzzy Noise@Speed,Particles,Bounce,Friction,Scale,Cylinder,Smear,Collide;;!;2;pal=64,sx=50,ix=200,c1=130,c2=30,c3=5,o3=1` | 55 |
| 203 | PS Ballpit | `mode_particlepit` | 2D, particle | `PS Ballpit@Speed,Intensity,Size,Hardness,Saturation,Cylinder,Walls,Ground;;!;2;pal=11,sx=100,ix=220,c1=120,c2=130,c3=31,o3=1` | 65 |
| 204 | PS Box | `mode_particlebox` | 2D, particle | `PS Box@!,Particles,Tilt,Hardness,Size,Random,Washing Machine,Sloshing;;!;2;pal=53,ix=50,c3=1,o1=1` | 74 |
| 205 | PS Attractor | `mode_particleattractor` | 2D, particle | `PS Attractor@Mass,Particles,Size,Collide,Friction,AgeColor,Move,Swallow;;!;2;pal=9,sx=100,ix=82,c1=2,c2=0` | 89 |
| 206 | PS Impact | `mode_particleimpact` | 2D, particle | `PS Impact@Launches,!,Force,Hardness,Blur,Cylinder,Walls,Collide;;!;2;pal=0,sx=32,ix=85,c1=70,c2=130,c3=0,o3=1` | 100 |
| 207 | PS Waterfall | `mode_particlewaterfall` | 2D, particle | `PS Waterfall@Speed,Intensity,Variation,Collide,Position,Cylinder,Walls,Ground;;!;2;pal=9,sx=15,ix=200,c1=32,c2=160,o3=1` | 65 |
| 208 | PS Spray | `mode_particlespray` | 2D, audio volume, particle | `PS Spray@Speed,!,Left/Right,Up/Down,Angle,Gravity,Cylinder/Square,Collide;;!;2v;pal=0,sx=150,ix=150,c1=220,c2=30,c3=21` | 78 |
| 209 | PS GEQ 2D | `mode_particleGEQ` | 2D, audio FFT, particle | `PS GEQ 2D@Speed,Intensity,Diverge,Bounce,Gravity,Cylinder,Walls,Floor;;!;2f;pal=0,sx=232,ix=200,c1=0,c3=9` | 65 |
| 210 | PS GEQ Nova | `mode_particlecenterGEQ` | 2D, audio FFT, particle | `PS GEQ Nova@Speed,Intensity,Rotation Speed,Color Change,Nozzle,,Direction;;!;2f;pal=13,ix=180,c1=0,c2=0,c3=8` | 63 |
| 211 | PS Ghost Rider | `mode_particleghostrider` | 2D, particle | `PS Ghost Rider@Speed,Spiral,Blur,Color Cycle,Spread,AgeColor,Walls;;!;2;pal=1,sx=70,ix=0,c1=220,c2=30,c3=21,o1=1` | 74 |
| 212 | PS Blobs | `mode_particleblobs` | 2D, audio volume, particle | `PS Blobs@Speed,Blobs,Size,Life,Blur,Wobble,Collide,Pulsate;;!;2v;sx=30,ix=64,c1=200,c2=130,c3=0,o3=1` | 70 |
| 213 | PS DripDrop | `mode_particleDrip` | 1D, particle | `PS DripDrop@Speed,!,Splash,Blur,Gravity,Rain,PushSplash,Smooth;,!;!;1;pal=0,sx=150,ix=25,c1=220,c2=30,c3=21` | 96 |
| 214 | PS Pinball | `mode_particlePinball` | 1D, particle | `PS Pinball@Speed,!,Size,Blur,Gravity,Collide,Rolling,Position Color;,!;!;1;pal=0,ix=220,c2=0,c3=8,o1=1` | 99 |
| 215 | PS Dancing Shadows | `mode_particleDancingShadows` | 1D, particle | `PS Dancing Shadows@Speed,!,Blur,Color Cycle,,Smear,Position Color,Smooth;,!;!;1;sx=100,ix=180,c1=0,c2=0` | 109 |
| 216 | PS Fireworks 1D | `mode_particleFireworks1D` | 1D, particle | `PS Fireworks 1D@Gravity,Explosion,Firing side,Blur,Color,Colorful,Trail,Smooth;,!;!;1;c2=30,o1=1` | 112 |
| 217 | PS Sparkler | `mode_particleSparkler` | 1D, particle | `PS Sparkler@Move,!,Saturation,Blur,Sparklers,Slide,Bounce,Large;,!;!;1;pal=0,sx=255,c1=0,c2=0,c3=6` | 65 |
| 218 | PS Hourglass | `mode_particleHourglass` | 1D, particle | `PS Hourglass@Interval,!,Color,Blur,Gravity,Colorflip,Start,Fast Reset;,!;!;1;pal=34,sx=50,ix=200,c1=140,c2=80,c3=4,o1=1,o2=1,o3=1` | 120 |
| 219 | PS Spray 1D | `mode_particle1Dspray` | 1D, particle | `PS Spray 1D@Speed(+/-),!,Position,Blur,Gravity(+/-),AgeColor,Bounce,Position Color;,!;!;1;sx=200,ix=220,c1=0,c2=0` | 45 |
| 220 | PS 1D Balance | `mode_particleBalance` | 1D, particle | `PS 1D Balance@!,!,Hardness,Blur,Tilt,Position Color,Wrap,Random;,!;!;1;pal=18,c2=0,c3=4,o1=1` | 74 |
| 221 | PS Chase | `mode_particleChase` | 1D, particle | `PS Chase@!,Density,Size,Hue,Blur,Playful,,Position Color;,!;!;1;pal=11,sx=50,c2=5,c3=0` | 88 |
| 222 | PS Starburst | `mode_particleStarburst` | 1D, particle | `PS Starburst@Chance,Fragments,Size,Blur,Cooling,Gravity,Colorful,Push;,!;!;1;pal=52,sx=150,ix=150,c1=120,c2=0,c3=21` | 53 |
| 223 | PS GEQ 1D | `mode_particle1DGEQ` | 1D, audio FFT, particle | `PS GEQ 1D@Speed,!,Size,Blur,,,,;,!;!;1f;pal=0,sx=50,ix=200,c1=0,c2=0,c3=0,o1=1,o2=1` | 67 |
| 224 | PS Fire 1D | `mode_particleFire1D` | 1D, particle | `PS Fire 1D@!,!,Cooling,Blur;,!;!;1;pal=35,sx=100,ix=50,c1=80,c2=100,c3=28,o1=1,o2=1` | 59 |
| 225 | PS Sonic Stream | `mode_particle1DsonicStream` | 1D, audio FFT, particle | `PS Sonic Stream@!,!,Color,Blur,Bin,Mod,Filter,Push;,!;!;1f;c3=0,o2=1` | 97 |
| 226 | PS Sonic Boom | `mode_particle1DsonicBoom` | 1D, audio FFT, particle | `PS Sonic Boom@!,!,Color,Position,Bin,Mod,Filter,Blur;,!;!;1f;c2=63,c3=0,o2=1` | 84 |
| 227 | PS Springy | `mode_particleSpringy` | 1D, audio FFT, particle | `PS Springy@Stiffness,Damping,Density,Hue,Mode,Smear,XL,AR;,!;!;1f;pal=54,c2=0,c3=23` | 162 |
| 228 | PS Galaxy | `mode_particlegalaxy` | 2D, particle | `PS Galaxy@!,!,Size,,Color,,Starfield,Trace;;!;2;pal=59,sx=80,c1=1,c3=4` | 92 |
| 229 | Color Clouds | `mode_ColorClouds` | 1D | `Color Clouds@!,!,Clouds,Colors,Distance,,,Cozy;;!;;sx=24,ix=32,c1=48,c2=64,c3=12,pal=0` | 68 |
