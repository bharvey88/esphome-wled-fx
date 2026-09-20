#!/usr/bin/env python3
"""Checks that the config-time name scanner still sees what the registry holds.

`components/wled_fx/effect_index.py` reads the effect and palette names out of
the C++ sources so that a typo in `effect:` or `palette:` is a config error
rather than a device that quietly renders something else. That scanner is a
regular expression over C++, so a source file written in a shape it does not
recognise would make it go blind, and a blind scanner rejects valid names.

This compares it against the simulator, which reports the names the registry
actually holds. Build the simulator first, then run this from the repository
root:

    cmake --build tools/sim/build
    python tools/check_effect_names.py
"""

from __future__ import annotations

import os
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "components" / "wled_fx"))

from effect_index import (  # noqa: E402
    effect_macro,
    effect_names,
    flag_overrides,
    one_dimensional_only,
    palette_names,
    two_dimensional_only,
)

LINE = re.compile(r"^(\S+)\s+(.+?)\s+flags=0x([0-9A-Fa-f]{2})\s")
FLAG_0D = 1 << 0
FLAG_1D = 1 << 1
FLAG_2D = 1 << 2
GUARD = re.compile(r"#define\s+WLED_FX_GROUP_\w+\s*((?:[^\n\\]*\\\s*\n)*[^\n]*)")
FX_MACRO = re.compile(r"WLED_FX_FX_\w+")


def find_sim() -> pathlib.Path:
    # WLED_FX_SIM lets a build somewhere else be named, which is how the WSL
    # wrapper points this at the Linux build rather than the stale Windows .exe
    # sitting in the checkout. See tools/wsl/sim.sh.
    if (override := os.environ.get("WLED_FX_SIM")):
        candidate = pathlib.Path(override)
        if not candidate.exists():
            sys.exit(f"WLED_FX_SIM names {candidate}, which does not exist")
        return candidate
    for name in ("wled_fx_sim", "wled_fx_sim.exe"):
        candidate = ROOT / "tools" / "sim" / "build" / name
        if candidate.exists():
            return candidate
    sys.exit("build the simulator first: cmake --build tools/sim/build")


def registered() -> list[tuple[str, int]]:
    out = subprocess.run(
        [str(find_sim()), "--list"], capture_output=True, text=True, check=True
    ).stdout
    entries = []
    for line in out.splitlines():
        match = LINE.match(line)
        if match is not None:
            entries.append((match.group(2).strip(), int(match.group(3), 16)))
    return entries


def guarded_macros() -> set[str]:
    macros: set[str] = set()
    for path in sorted((ROOT / "components" / "wled_fx").glob("wf_effects_*.cpp")):
        for guard in GUARD.findall(path.read_text(encoding="utf-8")):
            macros.update(FX_MACRO.findall(guard))
    return macros


def main() -> int:
    problems = []

    entries = registered()
    registry = [name for name, _ in entries]
    scanned = effect_names()
    if not registry:
        sys.exit("the simulator reported no effects")
    if registry != scanned:
        only_registry = sorted(set(registry) - set(scanned))
        only_scanned = sorted(set(scanned) - set(registry))
        if only_registry:
            problems.append(
                "the scanner missed these registered effects, so naming one in "
                f"YAML would be rejected: {only_registry}"
            )
        if only_scanned:
            problems.append(f"the scanner invented these effects: {only_scanned}")
        if not only_registry and not only_scanned:
            problems.append("the scanner returned the effects in a different order")

    guards = guarded_macros()
    unguarded = sorted(
        name for name in registry if effect_macro(name) not in guards
    )
    if unguarded:
        problems.append(
            "these effects are registered but no WLED_FX_GROUP_* guard names "
            f"them, so an 'effects:' allow-list cannot select them: {unguarded}"
        )

    # Which effects an output offers depends on these flags, and the answer has
    # to be the same in both places: the config validation refuses an effect the
    # configured layout cannot run, and the firmware refuses the same one at
    # runtime. A scanner that read the metadata differently, or missed the
    # override table in wf_registry.cpp, would reject configs the device would
    # have run or accept ones it will not.
    registry_2d_only = {
        name for name, flags in entries if not flags & (FLAG_1D | FLAG_0D)
    }
    scanned_2d_only = two_dimensional_only()
    if registry_2d_only != scanned_2d_only:
        problems.append(
            "the scanner and the registry disagree about which effects are 2D "
            f"only: registry only {sorted(registry_2d_only - scanned_2d_only)}, "
            f"scanner only {sorted(scanned_2d_only - registry_2d_only)}"
        )

    registry_1d_only = {name for name, flags in entries if not flags & FLAG_2D}
    scanned_1d_only = one_dimensional_only()
    if registry_1d_only != scanned_1d_only:
        problems.append(
            "the scanner and the registry disagree about which effects are 1D "
            f"only: registry only {sorted(registry_1d_only - scanned_1d_only)}, "
            f"scanner only {sorted(scanned_1d_only - registry_1d_only)}"
        )

    # An override naming an effect that is not registered is dead weight that
    # looks like it is doing something.
    registered_folded = {name.casefold() for name in registry}
    stray = sorted(set(flag_overrides()) - registered_folded)
    if stray:
        problems.append(
            "FLAG_OVERRIDES in wf_registry.cpp names effects that are not "
            f"registered: {stray}"
        )

    palettes = palette_names()
    if len(palettes) < 2 or palettes[0] != "Default":
        problems.append(f"the palette name scan looks wrong: {palettes[:4]}")

    if problems:
        for problem in problems:
            print(f"FAIL {problem}", file=sys.stderr)
        return 1

    print(
        f"name scanner agrees with the registry: {len(registry)} effects, "
        f"{len(palettes)} palettes"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
