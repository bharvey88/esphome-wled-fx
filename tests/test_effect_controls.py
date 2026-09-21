"""The metadata-to-controls parsing, against the C++ registration tables.

`components/wled_fx/effect_index.py` turns one effect's WLED metadata string
into the list of controls it uses, and `controls:` in YAML builds one entity
per item on that list. Everything the user sees, which entities exist and what
they are called, comes out of this one function, so it is worth pinning down.

The label wording and the fallback values these expect are not written here
either: they are read out of wf_registry.cpp and wf_registry.h, the same files
the firmware reads them from. A test that hard coded "Effect speed" would pass
after somebody changed the C++ and the firmware started saying something else.

    pytest tests

needs nothing but pytest; effect_index.py imports no ESPHome.
"""

from __future__ import annotations

from pathlib import Path
import sys

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "components" / "wled_fx"))

from effect_index import (  # noqa: E402
    CHECK_KEYS,
    COLOR_KEYS,
    SLIDER_KEYS,
    Control,
    controls_by_key,
    effect_controls,
    effect_metadata,
    effect_names,
    engine_defaults,
    label_defaults,
    metadata_defaults,
    metadata_for,
    palette_names,
)

LABELS = label_defaults()
ENGINE = engine_defaults()


def keys(metadata: str) -> list[str]:
    return [control.key for control in effect_controls(metadata)]


def by_key(metadata: str) -> dict[str, Control]:
    return controls_by_key(metadata)


# --- the C++ tables are found at all ----------------------------------------


def test_label_defaults_come_from_the_cpp():
    assert LABELS["slider"][:2] == ("Effect speed", "Effect intensity")
    assert len(LABELS["slider"]) == 5
    assert len(LABELS["check"]) == 3
    assert len(LABELS["color"]) == 3
    assert LABELS["palette"][0]


def test_engine_defaults_come_from_the_cpp():
    # The plain members of struct EffectDefaults, which is what the engine
    # writes when the metadata names no default.
    assert ENGINE["speed"] == 128
    assert ENGINE["custom3"] == 16
    assert ENGINE["check1"] is False


# --- "!" is WLED's own name for the control ----------------------------------


def test_bang_resolves_to_wleds_own_wording():
    controls = by_key("X@!,!,!,!,!,!,!,!;!,!,!;!;2")
    for index, key in enumerate(SLIDER_KEYS):
        assert controls[key].label == LABELS["slider"][index]
        assert controls[key].wled_default_label
    for index, key in enumerate(CHECK_KEYS):
        assert controls[key].label == LABELS["check"][index]
    for index, key in enumerate(COLOR_KEYS):
        assert controls[key].label == LABELS["color"][index]
    assert controls["palette"].label == LABELS["palette"][0]


def test_a_real_label_is_not_a_default_label():
    control = by_key("X@!,Spawning rate;;;2")["intensity"]
    assert control.label == "Spawning rate"
    assert not control.wled_default_label


def test_an_embedded_ui_default_is_not_part_of_the_name():
    # WLED writes "Label=7" when it wants its own slider to start at 7. The
    # number is for its UI, not part of what the control is called.
    assert by_key("X@Blur=64;;;2")["speed"].label == "Blur"


# --- an empty slot is a control the effect does not use ----------------------


def test_empty_slots_are_left_out():
    # Matrix: speed, intensity and custom1, one checkmark, two colours, and
    # deliberately no palette.
    assert keys(metadata_for("Matrix")) == [
        "speed",
        "intensity",
        "custom1",
        "check1",
        "color1",
        "color2",
    ]


def test_an_effect_with_no_palette_group_gets_no_palette():
    assert "palette" not in by_key("Sun Radiation@Variance,Brightness;;;2")


def test_an_effect_with_no_colour_group_gets_no_colours():
    controls = by_key(metadata_for("Fire 2012"))
    assert not any(key in controls for key in COLOR_KEYS)
    assert "palette" in controls


def test_a_numeric_palette_group_is_a_pinned_palette_and_hides_the_selector():
    # WLED reads a digit there as "this effect's palette is fixed", and hides
    # the selector rather than offering a choice that does nothing.
    assert "palette" not in by_key("X@!;;11;2")
    assert "palette" in by_key("X@!;;!;2")


def test_a_trailing_empty_slot_is_not_a_ninth_control():
    # Snow Fall's metadata ends its control group with a comma.
    controls = keys(metadata_for("Snow Fall"))
    assert len(controls) == len(set(controls))
    assert "check3" in controls


# --- defaults -----------------------------------------------------------------


def test_declared_defaults_win_over_the_engine_defaults():
    controls = by_key(metadata_for("Fire 2012"))
    assert controls["speed"].default == 64
    assert controls["intensity"].default == 160
    assert controls["custom2"].default == 128


def test_an_undeclared_slider_keeps_the_engine_default():
    assert by_key("X@!,!;;;2")["speed"].default == ENGINE["speed"]


def test_custom3_is_five_bits():
    control = by_key("X@,,,,Boost;;;2;c3=31")["custom3"]
    assert control.maximum == 31
    assert control.default == 31
    # Every other slider is a byte.
    assert by_key("X@Speed;;;2")["speed"].maximum == 255


def test_custom3_clamps_the_way_the_engine_does():
    # Engine::set_custom3() clamps to 31, so a metadata default above it is not
    # what the segment ends up holding.
    assert by_key("X@,,,,Boost;;;2;c3=200")["custom3"].default == 31


def test_every_ported_effect_keeps_custom3_in_range():
    for name in effect_names():
        control = controls_by_key(metadata_for(name)).get("custom3")
        if control is not None:
            assert 0 <= control.default <= 31, name


def test_a_checkmark_default_of_one_is_on():
    controls = by_key(metadata_for("PS Fire"))
    assert controls["check1"].default is True
    assert controls["check2"].default is False


def test_the_palette_default_is_the_declared_palette():
    assert by_key(metadata_for("Fire 2012"))["palette"].default == "Fire"


def test_an_effect_that_declares_no_palette_starts_on_palette_zero():
    # Upstream only writes the segment's palette when the metadata names one,
    # so the segment keeps what it had, which on a fresh boot is palette 0.
    assert by_key("X@!;;!;2")["palette"].default == palette_names()[0]


# --- the malformed upstream strings ------------------------------------------


def test_flow_stripe_reads_its_defaults_from_the_last_group():
    # Four groups, so the group its defaults live in is also the group the
    # dimensionality is read from. WLED reads the last group, not group four,
    # and so does this.
    metadata = metadata_for("Flow Stripe")
    assert metadata.count(";") == 3
    assert metadata_defaults(metadata) == {"pal": 11}
    controls = by_key(metadata)
    assert controls["speed"].label == "Hue speed"
    assert controls["intensity"].label == "Effect speed"
    assert controls["palette"].default == palette_names()[11]
    assert not any(key in controls for key in COLOR_KEYS)


def test_a_name_with_no_metadata_at_all_gets_wleds_fallback_set():
    # WLED shows the two standard sliders, all three colour slots and the
    # palette for an effect like this, because nothing says otherwise.
    assert keys("Solid") == [
        "speed",
        "intensity",
        "palette",
        "color1",
        "color2",
        "color3",
    ]
    assert metadata_for("Solid") == "Solid"


def test_a_group_that_is_not_there_is_not_a_control():
    # Three groups: sliders, colours, palette, and nothing after them.
    controls = by_key("Rainbow@!,Size;;!")
    assert set(controls) == {"speed", "intensity", "palette"}


# --- the WLED-MM extras -------------------------------------------------------


def test_the_wled_mm_effects_parse():
    mm = [
        "Meteor Smooth",
        "Party jerk",
        "Popcorn audio",
        "Multi Comet audio",
        "Fw Starburst audio",
        "Fireworks audio",
        "GEQ 3D",
        "Paintbrush",
        "Snow Fall",
    ]
    for name in mm:
        metadata = metadata_for(name)
        assert metadata is not None, name
        assert effect_controls(metadata), name


def test_paintbrush_keeps_its_three_checkmarks_and_two_colours():
    controls = by_key(metadata_for("Paintbrush"))
    assert [controls[key].label for key in CHECK_KEYS] == [
        "Color Chaos",
        "Anti-aliasing",
        "Phase Chaos",
    ]
    # Its middle colour slot is empty, so colour 2 is not offered and colour 3
    # is, which is the case an "is this slot used" test has to cover.
    assert "color1" in controls and "color2" not in controls
    assert controls["color3"].label == "Peaks"


def test_an_out_of_range_palette_default_does_not_crash():
    # Paintbrush declares pal=72 and this port has 72 palettes, 0 to 71. The
    # index is upstream's and is not corrected here, so all this has to do is
    # produce something a select can hold.
    assert metadata_defaults(metadata_for("Paintbrush"))["pal"] == 72
    assert by_key(metadata_for("Paintbrush"))["palette"].default in palette_names()


# --- every registered effect --------------------------------------------------


def test_every_registered_effect_parses_to_something_sane():
    names = palette_names()
    for metadata in effect_metadata():
        controls = effect_controls(metadata)
        seen = [control.key for control in controls]
        assert len(seen) == len(set(seen)), metadata
        for control in controls:
            assert control.label, metadata
            assert control.kind in ("slider", "check", "palette", "color")
            if control.kind == "slider":
                assert 0 <= control.default <= control.maximum, metadata
            elif control.kind == "check":
                assert isinstance(control.default, bool)
            elif control.kind == "palette":
                assert control.default in names, metadata
            else:
                assert control.default is None


@pytest.mark.parametrize("name", ["Matrix", "Fire 2012", "PS Fire"])
def test_the_three_example_effects_are_what_the_documentation_says(name):
    expected = {
        "Matrix": {
            "speed": "Effect speed",
            "intensity": "Spawning rate",
            "custom1": "Trail",
            "check1": "Custom color",
            "color1": "Spawn",
            "color2": "Trail",
        },
        "Fire 2012": {
            "speed": "Cooling",
            "intensity": "Spark rate",
            "custom2": "2D Blur",
            "custom3": "Boost",
            "palette": "Color palette",
        },
        "PS Fire": {
            "speed": "Speed",
            "intensity": "Intensity",
            "custom1": "Flame Height",
            "custom2": "Wind",
            "custom3": "Spread",
            "check1": "Smooth",
            "check2": "Cylinder",
            "check3": "Turbulence",
            "palette": "Color palette",
        },
    }[name]
    controls = by_key(metadata_for(name))
    assert {key: control.label for key, control in controls.items()} == expected
