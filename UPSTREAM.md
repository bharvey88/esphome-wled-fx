# Upstreaming to esphome/esphome

Stub, filled in as phases land. The plan for the staged pull requests is P7 in
`PLAN.md`; this file is where the concrete list lives once there is something to
submit.

## Status

Nothing submitted yet. P1 (foundation) is done; P2 to P6 come first.

## Constraints already known

From the recon in `recon/esphome-surface.md` and ESPHome's own `AGENTS.md`:

* PRs are opened against `dev`, titled `[wled_fx] Brief description`, with the
  full `.github/PULL_REQUEST_TEMPLATE.md` filled in.
* Over 1000 net non-test changed lines gets an automatic `too-big` label and an
  automated request for changes. The engine alone is well over that, so it has to
  be staged.
* A new component needs a `CODEOWNERS` entry, tests under
  `tests/components/wled_fx/`, and a linked documentation pull request against
  `esphome/esphome.io`, or the merge check fails on `needs-docs`,
  `needs-codeowners` or `needs-tests`.
* ESPHome's Python is MIT and its C++ is GPLv3. Everything WLED-derived must stay
  on the C++ side. The component is already built that way.
* Heap allocation after `setup()` is treated as a reliability bug. The canvas, the
  scratch row, the frame buffer and the select's name arena are all allocated once
  at setup; effect scratch is allocated on effect change only.

## Staging sketch

1. Engine only, no front end: canvas, segment, math, colour, palettes, registry,
   plus a handful of effects, with host tests.
2. Addressable light front end.
3. Display front end.
4. Effect batches, a few translation units at a time.
5. Particle system.
6. Audio source, FFT and the audio reactive effects.

Each one needs its own esphome.io documentation pull request linked from the body.
