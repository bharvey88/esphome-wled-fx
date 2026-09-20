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


def effect_names(component_dir: Path | None = None) -> list[str]:
    """Every effect display name in registration order."""
    directory = component_dir or COMPONENT_DIR
    names: list[str] = []
    for path in sorted(directory.glob("wf_effects_*.cpp")):
        block = _ENTRIES_BLOCK_RE.search(path.read_text(encoding="utf-8"))
        if block is None:
            continue
        for literals in _ENTRY_RE.findall(block.group(1)):
            metadata = "".join(_LITERAL_RE.findall(literals))
            names.append(metadata.split("@", 1)[0])
    return names


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
