"""Reads the effect and palette names out of the C++ sources at config time.

This file is independent work. It deliberately holds no effect names, palette
names or WLED parameter tables of its own: it scans the C++ sources, which are
where everything WLED derived lives. See PORTING.md.

Two callers use it: `wled_fx`'s own config validation, so a typo in `effect:` or
`palette:` is an error with a suggestion rather than a device that silently
renders the wrong thing, and `tools/check_effect_names.py`, which fails CI if
what this scanner sees ever stops matching what the simulator registers. That
second check is what keeps a parser that has quietly gone blind from rejecting
valid names.

It imports nothing from ESPHome so the CI check can run without it.
"""

from __future__ import annotations

import difflib
from pathlib import Path
import re

COMPONENT_DIR = Path(__file__).parent

# One EffectInfo entry: {"Name@metadata", mode_function}. The metadata is often
# long enough that it is written as several adjacent C++ string literals, which
# the compiler concatenates, so the name may not be in the first one.
_ENTRY_RE = re.compile(r'\{\s*((?:"[^"]*"\s*)+),\s*[A-Za-z_]\w*\s*\}')
_ENTRIES_BLOCK_RE = re.compile(r"const EffectInfo ENTRIES\[\] = \{(.*?)\n\};", re.S)
_PALETTE_BLOCK_RE = re.compile(
    r"const char \*const PALETTE_NAMES\[\] = \{(.*?)\n\};", re.S
)
_LITERAL_RE = re.compile(r'"([^"]*)"')

# Punctuation that carries meaning in an effect name and so has to survive into
# the derived identifier. Collapsing it to "_" made "Sparkle" and "Sparkle+" the
# same macro, so naming one in YAML silently pulled in both. Keep this table in
# step with effect_name_token() in tools/sim/main.cpp.
_NAME_TOKENS = {
    "+": "_PLUS",
    "&": "_AND",
    "/": "_SLASH",
    "#": "_HASH",
    "%": "_PCT",
    "*": "_STAR",
}


def effect_macro(name: str) -> str:
    """Turns an effect name from YAML into the macro the C++ guard tests."""
    expanded = "".join(_NAME_TOKENS.get(c, c) for c in name.upper())
    return "WLED_FX_FX_" + re.sub(r"[^A-Z0-9]+", "_", expanded).strip("_")


def effect_metadata(component_dir: Path | None = None) -> list[str]:
    """Every effect's WLED metadata string, in registration order."""
    directory = component_dir or COMPONENT_DIR
    metadata: list[str] = []
    for path in sorted(directory.glob("wf_effects_*.cpp")):
        block = _ENTRIES_BLOCK_RE.search(path.read_text(encoding="utf-8"))
        if block is None:
            continue
        for literals in _ENTRY_RE.findall(block.group(1)):
            metadata.append("".join(_LITERAL_RE.findall(literals)))
    return metadata


def effect_names(component_dir: Path | None = None) -> list[str]:
    """Every effect display name in registration order."""
    return [entry.split("@", 1)[0] for entry in effect_metadata(component_dir)]


# The override table in wf_registry.cpp, which is the one place an effect's
# dimensionality is corrected when its metadata string does not carry one. Read
# from there rather than repeated here, so the firmware and this scanner cannot
# disagree; the comment on FLAG_OVERRIDES says why the table exists at all.
_OVERRIDE_BLOCK_RE = re.compile(
    r"const FlagOverride FLAG_OVERRIDES\[\] = \{(.*?)\n\};", re.S
)
_OVERRIDE_ENTRY_RE = re.compile(
    r'\{\s*"([^"]*)"\s*,\s*((?:EFFECT_FLAG_\w+\s*(?:\|\s*)?)+)\}'
)
_FLAG_BITS = {
    "EFFECT_FLAG_0D": 1 << 0,
    "EFFECT_FLAG_1D": 1 << 1,
    "EFFECT_FLAG_2D": 1 << 2,
    "EFFECT_FLAG_VOLUME": 1 << 3,
    "EFFECT_FLAG_FFT": 1 << 4,
}


def flag_overrides(component_dir: Path | None = None) -> dict[str, int]:
    """Effect name, case folded, to the flag byte that replaces the parsed one."""
    directory = component_dir or COMPONENT_DIR
    text = (directory / "wf_registry.cpp").read_text(encoding="utf-8")
    block = _OVERRIDE_BLOCK_RE.search(text)
    if block is None:
        return {}
    out: dict[str, int] = {}
    for name, flags in _OVERRIDE_ENTRY_RE.findall(block.group(1)):
        value = 0
        for token in re.findall(r"EFFECT_FLAG_\w+", flags):
            value |= _FLAG_BITS.get(token, 0)
        out[name.casefold()] = value
    return out


def parse_flags(metadata: str) -> int:
    """The dimensionality and audio flags in an effect's metadata string.

    The fourth ';' group is the flag set: '0', '1' and '2' for the three
    dimensionalities, plus 'v' and 'f' for the two audio kinds. Matches
    effect_defaults() in wf_registry.cpp, which reads the same group and,
    crucially, falls back to 1D when the group names no dimension at all.
    Disagreeing with it would mean rejecting a config the engine would have run.
    tools/check_effect_names.py fails if the two ever drift.
    """
    groups = metadata.split("@", 1)[-1].split(";")
    dimensions = groups[3].strip() if len(groups) > 3 else ""
    value = 0
    for char, bit in (
        ("0", 1 << 0),
        ("1", 1 << 1),
        ("2", 1 << 2),
        ("v", 1 << 3),
        ("f", 1 << 4),
    ):
        if char in dimensions:
            value |= bit
    return value or (1 << 1)


def effect_flags(component_dir: Path | None = None) -> dict[str, int]:
    """Every effect's flag byte, keyed by display name, overrides applied."""
    overrides = flag_overrides(component_dir)
    out: dict[str, int] = {}
    for entry in effect_metadata(component_dir):
        name = entry.split("@", 1)[0]
        out[name] = overrides.get(name.casefold(), parse_flags(entry))
    return out


def runs_in_1d(metadata: str) -> bool:
    """True unless the effect's metadata says it is 2D only."""
    return bool(parse_flags(metadata) & ((1 << 0) | (1 << 1)))


def two_dimensional_only(component_dir: Path | None = None) -> set[str]:
    """The effects that render nothing but a solid fill on a 1D canvas."""
    return {
        name
        for name, flags in effect_flags(component_dir).items()
        if not flags & ((1 << 0) | (1 << 1))
    }


def one_dimensional_only(component_dir: Path | None = None) -> set[str]:
    """The effects that only reach a matrix through WLED's 1D to 2D mapping."""
    return {
        name
        for name, flags in effect_flags(component_dir).items()
        if not flags & (1 << 2)
    }


def available_effects(
    two_dimensional: bool,
    include_1d: bool = False,
    component_dir: Path | None = None,
) -> set[str]:
    """The effect names an output of this shape offers.

    The same rule as effect_available() in wf_registry.cpp. A 1D output offers
    what can run on a line; a 2D one offers what is written for a matrix, plus
    the 1D-only effects when the configuration opted in to them.
    """
    flags = effect_flags(component_dir)
    if not two_dimensional:
        return {n for n, f in flags.items() if f & ((1 << 0) | (1 << 1))}
    return {
        n
        for n, f in flags.items()
        if f & (1 << 2) or (include_1d and f & ((1 << 0) | (1 << 1)))
    }


def palette_names(component_dir: Path | None = None) -> list[str]:
    """Every palette display name, in palette ID order."""
    directory = component_dir or COMPONENT_DIR
    text = (directory / "wf_palette_util.cpp").read_text(encoding="utf-8")
    block = _PALETTE_BLOCK_RE.search(text)
    if block is None:
        return []
    return _LITERAL_RE.findall(block.group(1))


def suggestion(name: str, candidates: list[str]) -> str:
    """' Did you mean "X"?' for a near miss, or an empty string."""
    folded = {candidate.casefold(): candidate for candidate in candidates}
    close = difflib.get_close_matches(name.casefold(), folded, n=1, cutoff=0.6)
    if not close:
        return ""
    return f' Did you mean "{folded[close[0]]}"?'
