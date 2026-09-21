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

from dataclasses import dataclass
import difflib
import functools
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


# --- per-effect controls -----------------------------------------------------
#
# Every effect's metadata string says which of the eight generic controls, the
# palette and the three colour slots it uses, and what WLED calls each of them.
# `controls:` in YAML turns that into one named ESPHome entity per used control,
# at compile time, so a configuration pinned to one effect gets a "Spawning
# rate" slider rather than a "Custom 1" and a line of text explaining it.
#
# This is the same reading of the string as effect_labels() and
# effect_defaults() in wf_registry.cpp, and as setEffectParameters() in WLED
# 16.0.1's data/index.js, which is where the hiding and the "!" fallbacks come
# from. The wording of those fallbacks is not repeated here: it is read out of
# wf_registry.cpp, so the C++ stays the one source of truth and the two cannot
# drift. tools/check_effect_names.py fails if they ever do.

# The YAML key for each control, in the order the metadata lists them. They are
# the keys the rest of the schema and the actions already use, so nobody has to
# learn a second set of names for the same eight controls.
SLIDER_KEYS = ("speed", "intensity", "custom1", "custom2", "custom3")
CHECK_KEYS = ("check1", "check2", "check3")
COLOR_KEYS = ("color1", "color2", "color3")
PALETTE_KEY = "palette"
CONTROL_KEYS = SLIDER_KEYS + CHECK_KEYS + (PALETTE_KEY,) + COLOR_KEYS

# The keys the metadata's defaults group uses for the sliders and checkmarks,
# in the same order as the tuples above. PORTING.md section 4 has the full list.
_SLIDER_DEFAULT_KEYS = ("sx", "ix", "c1", "c2", "c3")
_CHECK_DEFAULT_KEYS = ("o1", "o2", "o3")

# custom3 is five bits everywhere, which PORTING.md section 4 explains; every
# other slider is a byte.
SLIDER_MAXIMUM = {key: (31 if key == "custom3" else 255) for key in SLIDER_KEYS}


@dataclass(frozen=True)
class Control:
    """One control an effect uses, ready to become an entity.

    `label` is what WLED calls it, with "!" already resolved to WLED's own
    wording. `wled_default_label` records that it was a "!", which is worth
    saying in a review document and nowhere else.
    """

    key: str
    # "slider", "check", "palette" or "color".
    kind: str
    label: str
    wled_default_label: bool
    # What the control starts at: an int for a slider, a bool for a checkmark,
    # a palette name for the palette, None for a colour slot.
    default: int | bool | str | None
    # Sliders only.
    maximum: int | None = None


_LABEL_DEFAULTS_RE = re.compile(
    r"const char \*const (SLIDER|CHECK|COLOR)_LABEL_DEFAULTS\[\d+\] = \{(.*?)\};", re.S
)
_PALETTE_LABEL_RE = re.compile(r'const char \*const PALETTE_LABEL_DEFAULT = "([^"]*)"')
_DEFAULTS_STRUCT_RE = re.compile(r"struct EffectDefaults \{(.*?)\n\};", re.S)
_DEFAULTS_FIELD_RE = re.compile(r"(uint8_t|bool)\s+(\w+)\{([^}]*)\}")


@functools.lru_cache(maxsize=None)
def label_defaults(component_dir: Path | None = None) -> dict[str, tuple[str, ...]]:
    """WLED's own wording for a control whose label is "!", read from the C++.

    SLIDER_LABEL_DEFAULTS and its two neighbours in wf_registry.cpp are what the
    firmware publishes, so they are what an entity name has to say as well.
    """
    directory = component_dir or COMPONENT_DIR
    text = (directory / "wf_registry.cpp").read_text(encoding="utf-8")
    out: dict[str, tuple[str, ...]] = {}
    for kind, block in _LABEL_DEFAULTS_RE.findall(text):
        out[kind.lower()] = tuple(_LITERAL_RE.findall(block))
    palette = _PALETTE_LABEL_RE.search(text)
    out["palette"] = (palette.group(1),) if palette else ()
    return out


@functools.lru_cache(maxsize=None)
def engine_defaults(component_dir: Path | None = None) -> dict[str, int | bool]:
    """What a control holds when the metadata names no default for it.

    The member initialisers on `struct EffectDefaults` in wf_registry.h, which
    is what the engine writes into the segment.
    """
    directory = component_dir or COMPONENT_DIR
    block = _DEFAULTS_STRUCT_RE.search(
        (directory / "wf_registry.h").read_text(encoding="utf-8")
    )
    out: dict[str, int | bool] = {}
    if block is None:
        return out
    for kind, name, value in _DEFAULTS_FIELD_RE.findall(block.group(1)):
        if kind == "bool":
            out[name] = value == "true"
        elif value.isdigit():
            out[name] = int(value)
        # The rest of the struct is initialised from named constants rather
        # than literals, and none of it is a control this reads.
    return out


def _split_group(group: str | None, count: int) -> list[str | None]:
    """One metadata group into `count` labels, None where the control is unused.

    An empty item is a control the effect does not use, which is how WLED hides
    it. An item written "Label=7" carries a default for WLED's own UI rather
    than part of a name, so everything from the '=' is dropped. Matches
    split_labels() in wf_registry.cpp.
    """
    out: list[str | None] = [None] * count
    if group is None:
        return out
    for index, item in enumerate(group.split(",")[:count]):
        item = item.split("=", 1)[0]
        if item:
            out[index] = item
    return out


def _metadata_groups(metadata: str) -> list[str] | None:
    """The ';' groups after the display name, or None when the string has none."""
    _, separator, rest = metadata.partition("@")
    if not separator:
        return None
    return rest.split(";")


def _byte(text: str) -> int:
    """strtol() and then a cast to uint8_t, which is what the C++ parser does."""
    match = re.match(r"\s*[+-]?\d+", text)
    return int(match.group(0)) % 256 if match else 0


def metadata_defaults(metadata: str) -> dict[str, int]:
    """The `key=value` pairs in the metadata's last ';' group.

    WLED reads the last group rather than group index 4, and so does
    effect_defaults() in wf_registry.cpp. That is what makes the handful of
    malformed upstream strings work: Flow Stripe is the one in this port, with
    four groups, so the group its defaults live in is also the group the
    dimensionality is read from.
    """
    groups = _metadata_groups(metadata)
    if groups is None:
        return {}
    out: dict[str, int] = {}
    for item in groups[-1].split(","):
        key, separator, value = item.partition("=")
        if not separator:
            continue
        out[key.strip()] = _byte(value)
    return out


def effect_controls(metadata: str, component_dir: Path | None = None) -> list[Control]:
    """Every control the effect uses, in WLED's order, ready to become entities.

    The one place a metadata string becomes a control list. The schema and the
    codegen both call it, so what a configuration is checked against and what
    gets built cannot disagree.
    """
    labels = label_defaults(component_dir)
    engine = engine_defaults(component_dir)
    declared = metadata_defaults(metadata)
    groups = _metadata_groups(metadata)

    if groups is None:
        # Nothing but a display name. WLED shows the two standard sliders, all
        # three colour slots and the palette for an effect like this, because
        # it has nothing that says otherwise, and effect_labels() in
        # wf_registry.cpp makes the same choice for the same reason.
        slider_labels: list[str | None] = ["!", "!", None, None, None]
        check_labels: list[str | None] = [None, None, None]
        color_labels: list[str | None] = ["!", "!", "!"]
        palette_label: str | None = "!"
    else:
        controls = _split_group(groups[0], 8)
        slider_labels = controls[:5]
        check_labels = controls[5:]
        color_labels = _split_group(groups[1] if len(groups) > 1 else None, 3)
        palette_label = _split_group(groups[2] if len(groups) > 2 else None, 1)[0]
        # An all digits palette group pins the palette, and WLED hides the
        # selector rather than offering a choice that does nothing.
        if palette_label is not None and palette_label.isdigit():
            palette_label = None

    def resolve(label: str, fallback: str) -> tuple[str, bool]:
        return (fallback, True) if label == "!" else (label, False)

    out: list[Control] = []
    for index, (key, label) in enumerate(zip(SLIDER_KEYS, slider_labels)):
        if label is None:
            continue
        text, is_default = resolve(label, labels["slider"][index])
        default = declared.get(_SLIDER_DEFAULT_KEYS[index], engine[key])
        # Engine::set_custom3() clamps, so a metadata default above 31 is not
        # what the segment ends up holding.
        default = min(default, SLIDER_MAXIMUM[key])
        out.append(
            Control(key, "slider", text, is_default, default, SLIDER_MAXIMUM[key])
        )

    for index, (key, label) in enumerate(zip(CHECK_KEYS, check_labels)):
        if label is None:
            continue
        text, is_default = resolve(label, labels["check"][index])
        declared_check = declared.get(_CHECK_DEFAULT_KEYS[index])
        default = engine[key] if declared_check is None else bool(declared_check)
        out.append(Control(key, "check", text, is_default, default))

    if palette_label is not None:
        text, is_default = resolve(palette_label, labels["palette"][0])
        # Upstream writes the segment's palette only when the metadata names
        # one, so an effect that declares none starts on whatever the segment
        # already had, which on a fresh boot is palette 0.
        names = palette_names(component_dir)
        index = declared.get("pal", 0)
        default = names[index] if index < len(names) else names[0]
        out.append(Control(PALETTE_KEY, "palette", text, is_default, default))

    for index, (key, label) in enumerate(zip(COLOR_KEYS, color_labels)):
        if label is None:
            continue
        text, is_default = resolve(label, labels["color"][index])
        out.append(Control(key, "color", text, is_default, None))

    return out


def controls_by_key(
    metadata: str, component_dir: Path | None = None
) -> dict[str, Control]:
    """effect_controls() keyed by the YAML name of each control."""
    return {control.key: control for control in effect_controls(metadata, component_dir)}


def metadata_for(name: str, component_dir: Path | None = None) -> str | None:
    """One effect's verbatim metadata string, matched the way names always are."""
    for entry in effect_metadata(component_dir):
        if entry.split("@", 1)[0].casefold() == name.casefold():
            return entry
    return None
