# Round 2 tooling notes

What still measures badly, ordered by how much damage it did this round. Round
1's T1 to T8 were all fixed and all of the fixes hold: the direction estimator
no longer aliases, the attribution refuses to answer rather than guessing,
`--reference` and `--port` are repeatable, the palette is no longer latched at
`call == 0`, the white-channel floor is called out, the hue reading is
suppressed below 40 lit pixels, and `subsample()` survives a panel that is not
a whole multiple of the live view. The list below is what round 2 hit.

## T9 The thresholds were never checked against the instrument

`tools/compare/compare.py:61-66` flags at 40 counts of brightness, 0.25 of
coverage, a hue distance of 0.35 and a speed ratio of 1.25. Three of the four
are below the p95 of the device's disagreement with itself, and the fourth is
below its p90. `NOISE.md` has the table and the replacement numbers. Nothing in
the tool records where its own constants came from, so nobody noticed.

**Improvement.** Put the per-class floors from `NOISE.md` in the code with a
comment naming the run pair they were derived from, take the class from the
effect's metadata rather than a name prefix, and make the report print the
floor beside every flag it raises. A flag that does not say what it is being
compared against is a flag a reader has to re-derive.

## T10 The hue distance is still the biggest single source of false flags, and the report does not print its spread

Twelve of the 25 surviving flags are hue, and every one of them is at or below
the same effect's hue distance between two runs of the same device. The
lit-pixel suppression added in round 1 helps only at the very bottom: Wipe
Random, Sweep Random and TV Simulator all light 1024 pixels a frame and all
score 1.00 against themselves.

Blink Rainbow is the clean illustration of what is missing. The report gives it
0.75; the three port runs actually score 0.75, 0.04 and 0.24 against the same
device run. The tool pools brightness and coverage across runs and prints the
spread, and does neither for hue.

**Improvement.** Pool the hue histogram across runs the way brightness is
pooled, print the per-side spread, and score the *minimum* port-against-device
distance rather than the median, because an effect that draws a random colour
will sometimes agree and a broken one never will. Better still, compare the
sorted hue histograms rather than the histograms in place, which is what
"the same rainbow at a different phase" needs.

## T11 `frozen` and `all_black` are booleans over a threshold, and the threshold sits where the data is

Fifteen of 216 effects flip `frozen` between two runs of the same firmware, and
three flip `all_black`. That is a 7 percent false-flag rate on a binary the
report states as fact ("WLED animates and the port does not"). Perlin Move,
Color Clouds, PS Attractor and Strobe were all flagged this round on a boolean
the device cannot reproduce.

**Improvement.** Drop the boolean from the report and print the two mean frame
change numbers with both sides' spreads. If a phrase is wanted, require the two
ranges to be disjoint before using it.

## T12 A six second window cannot see six of these effects, and round 2 had to go outside the harness to find out

Round 1 argued from theory that Sweep, Wipe, Tri Wipe, Tartan, Slow Transition,
PS Galaxy, Halloween Eyes, Lightning and PS Starburst are slower than the
window. Round 2 measured it, by running `capture_wled.py --seconds 30` and
`capture_port.py --seconds 30` by hand and analysing the frames in a scratch
script. At 30 s: Sweep 0.97, Wipe 0.97, Tri Wipe 0.92, Slow Transition 1.00,
Fill Noise 1.00, PS Starburst 1.17. All of them resolve.

Neither tool has a per-effect capture length, so this cannot be part of a
normal run, and `capture_port.py` has no way to say "only these effects at this
length" without also re-capturing all 223 (`--effects` exists but
`--match-reference` does not restrict the list, so a 7 effect reference folder
still produced a 223 effect port run).

**Improvement.** A `SLOW_EFFECTS` table keyed by name with a capture length,
consulted by both capture tools and recorded in `meta.json`, so the comparison
can refuse to compare a 6 s reference against a 30 s port. Derive the initial
table from the effects whose body calls `beatsin` below about 12 bpm or whose
delay expression can exceed the window: the three that matter here are
`hw_random8(255 - speed) * 100` in Lightning, `10 + hw_random16(255 - speed)`
in PS Starburst and the speed slider in Slow Transition. And make
`--match-reference` imply the reference folder's effect list unless `--effects`
overrides it.

## T13 The engine attribution has still never been run at scale

`capture_engine.py --match-reference` was built in round 1 and this round's
report says, correctly, "No engine simulator capture was given, so nothing
below says whether a difference is in the effect engine or in the ESPHome
display front end." It has now been unavailable for two rounds. R2-1 is
precisely a front-end finding, and a working attribution would have pointed at
it in round 1.

**Improvement.** Run it as part of the comparison rather than as an option, and
fail the run if the engine capture's recorded controls do not match.

## T14 Nothing in the harness can see the output stage, which is where R2-1 lives

Both capture paths are deliberately pre-gamma: the live view sends
`strip.getPixelColor()` and the snapshot harness runs at `gamma_correct: 1.0`.
That is the right choice for comparing engines, and it means the comparison is
structurally blind to the single largest difference a user will see. R2-1 was
found by reading the two output paths, not by measuring them, and nothing in
CI would catch a regression in either.

**Improvement.** A host test that pushes a known canvas through
`WledFxDisplay`'s frame conversion at a given `gamma_correct` and asserts the
bytes, and the same for `WledFxLightEffect` through a stub
`AddressableLight` with a known correction. Twenty lines each, and they pin the
two numbers in R2-1's table.

## T15 The control layer has no harness at all

R2-2 and R2-3 are the biggest behavioural findings of this round and neither is
visible to any tool in the repository. The comparison forces every control from
the reference, which is right for comparing renders and means the defaults
themselves are never compared. `tools/check_effect_names.py` checks names, and
`wled_fx_effect_test` checks that an override survives an effect change, and
nothing checks what the defaults actually are.

**Improvement.** A check that runs `effect_defaults()` over all 223 effects and
compares the result against a checked-in table derived from the device's
`/json/state` readback, which the reference capture already records in every
`meta.json` as `applied_state_fields`. That table would have caught all 112
palette rows on the first run, and it costs one Python file and no device time.

## T16 Two capture runs of the device is the minimum, and the tools do not say so

The whole of this round's re-ranking came from having a second reference run.
`capture_wled.py` has no notion of a run, writes into a fixed `captures/`
directory and skips anything already captured, so a second pass means choosing
a new `--outdir` by hand and remembering to. `compare.py` accepts repeated
`--reference` but does not warn when given only one.

**Improvement.** Warn, loudly, in the report when either side has a single run,
and say in `tools/compare/README.md` that a single reference run is not a
measurement. Better: have `capture_wled.py` write `captures/run-N/` and pick
the next N itself.

## T17 Smaller things

* `capture_port.py` writes its output relative to a path that must be a WSL
  path. Given a Windows path it silently creates a directory whose name
  contains backslashes inside the distribution and reports success. A
  `--outdir` that starts with a drive letter should be rejected or translated.
* Two concurrent `capture_port.py` runs share `/root/wfx/snapshots` and the
  second produces 223 effects with no frames and then fails in `build_index`
  writing `index.json`. The workdir should carry the process id.
* `tools/wsl/wfx.ps1 snapshot` swallowed all output when invoked from a
  non-interactive shell in this session, which is why every run here called
  `wsl.exe` directly. Worth a look.
* The simulator's `--anim` writes `<sanitised lower case name>.rgb` while
  `effect_index.effect_names()` returns the display name, so anything analysing
  those files has to re-derive the mapping. Write a `manifest.json` beside them.
* `compare.py` prints "output gamma not recorded" for the device side. It is
  knowable: WLED's `/json/cfg` carries `light.gc.val`, and the reference tool
  could record it read-only alongside `info.json`.
* The reference tool's `SUMMARY.md` guesses why an effect is black. Two of its
  three guesses for this device ("audio-reactive", "triggers sparsely") are
  right and the third is a paragraph of hedging. With two runs it could say
  "black in both runs" or "black in one of two", which is the useful fact.
