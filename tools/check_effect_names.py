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
    effect_controls,
    effect_macro,
    effect_metadata,
    effect_names,
    flag_overrides,
    metadata_defaults,
    one_dimensional_only,
    palette_names,
    two_dimensional_only,
)

LINE = re.compile(r"^(\S+)\s+(.+?)\s+flags=0x([0-9A-Fa-f]{2})\s")
DEFAULTS_LINE = re.compile(
    r"^\S+\s+(.+?)\s+flags=0x[0-9A-Fa-f]{2}\s+pal=(-?\d+)\s+sx=(\d+)\s+ix=(\d+)\s"
)
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


def sim_output(*args: str) -> str:
    return subprocess.run(
        [str(find_sim()), *args], capture_output=True, text=True, check=True
    ).stdout


def registered() -> list[tuple[str, int]]:
    entries = []
    for line in sim_output("--list").splitlines():
        match = LINE.match(line)
        if match is not None:
            entries.append((match.group(2).strip(), int(match.group(3), 16)))
    return entries


def control_problems() -> list[str]:
    """The controls `controls:` builds, against what the registry holds.

    `controls: true` creates one entity per control the pinned effect uses,
    named what WLED names it, and every bit of that comes from reading the
    effect's metadata string in Python. The firmware reads the same string in
    C++. If the two readings ever part company, a configuration gets entities
    for controls the effect does not have, or misses ones it does, and nothing
    else would notice.

    So: the strings the scanner sees are checked against `--list-meta`, which
    prints them verbatim from the registry, and the defaults it parses out of
    them are checked against `--list`, which prints what the engine will
    actually write into the segment.
    """
    problems: list[str] = []

    registry_meta = []
    for line in sim_output("--list-meta").splitlines():
        if "\t" in line:
            registry_meta.append(line.split("\t", 1)[1])
    scanned_meta = effect_metadata()
    if registry_meta != scanned_meta:
        only_registry = sorted(set(registry_meta) - set(scanned_meta))
        only_scanned = sorted(set(scanned_meta) - set(registry_meta))
        problems.append(
            "the metadata strings the scanner reads are not the ones the "
            f"registry holds: registry only {only_registry[:3]}, scanner only "
            f"{only_scanned[:3]}"
        )
        return problems

    by_name = {entry.split("@", 1)[0]: entry for entry in scanned_meta}
    for line in sim_output("--list").splitlines():
        match = DEFAULTS_LINE.match(line)
        if match is None:
            continue
        name, palette, speed, intensity = match.groups()
        name = name.strip()
        metadata = by_name.get(name)
        if metadata is None:
            continue
        controls = {c.key: c for c in effect_controls(metadata)}
        # The sliders, when the effect offers them. A control it does not offer
        # has no entity and so no default worth comparing.
        for key, expected in (("speed", int(speed)), ("intensity", int(intensity))):
            if key in controls and controls[key].default != expected:
                problems.append(
                    f"{name}: the scanner reads {key} as "
                    f"{controls[key].default} and the registry as {expected}"
                )
        # pal prints -1 when the metadata declares none, and the scanner reads
        # the same absence as "the segment keeps palette 0".
        declared = metadata_defaults(metadata).get("pal")
        if int(palette) != (-1 if declared is None else declared):
            problems.append(
                f"{name}: the scanner reads pal as {declared} and the registry "
                f"as {palette}"
            )
    return problems


def guarded_macros() -> set[str]:
    macros: set[str] = set()
    for path in sorted((ROOT / "components" / "wled_fx").glob("wf_effects_*.cpp")):
        for guard in GUARD.findall(path.read_text(encoding="utf-8")):
            macros.update(FX_MACRO.findall(guard))
    return macros


def segment_budget_problems() -> list[str]:
    """The per-platform effect budget has to be upstream's table, in its order.

    Round 2 found three ways this can be wrong without anything noticing: a
    branch spelled with a symbol ESPHome never defines, so it can never be
    taken; the PSRAM test before the ESP32-S2 test, where upstream has it after,
    which gives an S2 with PSRAM twice the segments upstream allows it; and no
    ESP8266 row at all. None of it is visible on the one board the port is
    built for, so it is checked here against WLED's own header.
    """
    problems: list[str] = []
    port = (ROOT / "components" / "wled_fx" / "wf_segment.h").read_text(encoding="utf-8")

    # WLED's own header, when there is a checkout of it beside this tree. It is
    # not in the repository and CI does not have it, so the ladder is checked
    # against the recorded table either way and against upstream as well when
    # upstream is there.
    upstream_path = ROOT / "refs" / "WLED" / "wled00" / "FX.h"
    if upstream_path.exists():
        upstream = upstream_path.read_text(encoding="utf-8", errors="replace")
        block = re.search(r"#ifdef ESP8266(.*?)#define FAIR_DATA_PER_SEG", upstream, re.S)
        if block is None:
            return ["could not find the MAX_SEGMENT_DATA block in refs/WLED/wled00/FX.h"]
        numbers = re.findall(
            r"#define\s+MAX_(?:NUM_SEGMENTS|SEGMENT_DATA)\s+\(?(\d+)", block.group(1)
        )
        # ESP8266 16 / 6k, S2 32 / 20k, then PSRAM 64, no PSRAM 32, 64k for both.
        if numbers != ["16", "6", "32", "20", "64", "32", "64"]:
            problems.append(f"WLED's own budget table has changed shape: {numbers}")
            return problems

    expected = [
        ("WLED_FX_ESP8266", 16, 6),
        ("WLED_FX_ESP32S2", 32, 20),
        ("WLED_FX_PSRAM", 64, 64),
        (None, 32, 64),  # the #else
    ]
    branches = re.findall(
        r"#(?:(?:el)?if\s+(WLED_FX_\w+)|else)\s*\n"
        r"inline constexpr unsigned MAX_NUM_SEGMENTS = (\d+);\s*\n"
        r"inline constexpr unsigned MAX_SEGMENT_DATA = (\d+) \* 1024;",
        port,
    )
    found = [(sym or None, int(segs), int(kb)) for sym, segs, kb in branches]
    if found != expected:
        problems.append(
            "wf_segment.h's budget ladder is not upstream's table in upstream's "
            f"order: {found}"
        )
    return problems


def palette_problems() -> list[str]:
    """The 72 palettes, against WLED's own list and its own tables.

    A palette is three things that have to agree: the name the select offers,
    the slot that name resolves to, and the bytes that slot loads. The name
    list is checked against `JSON_palette_names`, which is the same list a
    device serves from `/json/palettes`, and the two table arrays are checked
    against `palettes.cpp`, entry for entry and in order. Renaming one palette
    or reordering the gradient array would leave every effect rendering in
    somebody else's colours, which no brightness or hue metric would flag
    because the colours would all still be WLED's.

    Skipped when there is no checkout of WLED beside this tree, which is the
    case in CI.
    """
    problems: list[str] = []
    palettes = palette_names()
    if len(palettes) < 2 or palettes[0] != "Default":
        problems.append(f"the palette name scan looks wrong: {palettes[:4]}")
        return problems

    upstream_dir = ROOT / "refs" / "WLED" / "wled00"
    fx_fcn = upstream_dir / "FX_fcn.cpp"
    upstream_palettes = upstream_dir / "palettes.cpp"
    if not fx_fcn.exists() or not upstream_palettes.exists():
        return problems

    names_block = re.search(
        r'JSON_palette_names\[\]\s*PROGMEM\s*=\s*R"=====\(\[(.*?)\]\)=====";',
        fx_fcn.read_text(encoding="utf-8", errors="replace"),
        re.S,
    )
    if names_block is None:
        problems.append("could not find JSON_palette_names in refs/WLED/wled00/FX_fcn.cpp")
    else:
        upstream_names = re.findall(r'"([^"]*)"', names_block.group(1))
        if palettes != upstream_names:
            differing = [
                f"{i}: {a!r} vs {b!r}"
                for i, (a, b) in enumerate(zip(palettes, upstream_names))
                if a != b
            ]
            problems.append(
                "the palette list is not WLED's: "
                f"{len(palettes)} here against {len(upstream_names)} upstream"
                + (f", first differences {differing[:5]}" if differing else "")
            )

    # The two table arrays, which are what a name actually loads.
    def table_order(text: str, array: str, symbol: str) -> list[str]:
        block = re.search(rf"{array}\s*\[\]\s*(?:PROGMEM\s*)?=\s*\{{(.*?)\}}\s*;", text, re.S)
        return re.findall(symbol, block.group(1)) if block else []

    port_text = (ROOT / "components" / "wled_fx" / "wf_palettes.cpp").read_text(encoding="utf-8")
    up_text = upstream_palettes.read_text(encoding="utf-8", errors="replace")
    for port_array, up_array, symbol, label in (
        ("GRADIENT_PALETTES", "gGradientPalettes", r"(\w+_gp)", "gradient"),
        ("FASTLED_PALETTES", "fastledPalettes", r"&(\w+)", "FastLED"),
    ):
        here = table_order(port_text, port_array, symbol)
        there = table_order(up_text, up_array, symbol)
        if not here or not there:
            problems.append(f"could not read the {label} palette array from both sides")
        elif here != there:
            problems.append(f"the {label} palette array is not upstream's, in upstream's order")
    return problems


def main() -> int:
    problems = segment_budget_problems()

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
    problems.extend(palette_problems())
    problems.extend(control_problems())

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
