"""What `controls:` accepts, what it builds and what it refuses.

The parsing has its own file. This one is about the schema around it: which
entities a configuration ends up with, and whether the four ways of misusing
it say something a person can act on.

Most of it runs the real `esphome config` over a temporary file, because the
interesting errors are the ones that need the whole configuration in view: a
generic `number:` platform pointing at a controller that already has named
controls is two configurations disagreeing, and nothing smaller than final
validation can see it.

    pytest tests

needs pytest and the ESPHome this repository is built with.
"""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import textwrap

import pytest

ROOT = Path(__file__).resolve().parent.parent
COMPONENTS = str(ROOT / "components").replace("\\", "/")

sys.path.insert(0, str(ROOT / "components"))

from wled_fx import _check_named_controls  # noqa: E402

DISPLAY = f"""
esphome:
  name: wled-fx-test

esp32:
  board: esp32-s3-devkitc-1
  framework:
    type: esp-idf

external_components:
  - source:
      type: local
      path: {COMPONENTS}

display:
  - platform: hub75
    id: matrix
    board: apollo-automation-m1-rev6
    panel_width: 64
    panel_height: 64
    update_interval: never
    auto_clear_enabled: false
    gamma_correct: LINEAR
"""

STRIP = f"""
esphome:
  name: wled-fx-test

esp32:
  board: esp32dev
  framework:
    type: esp-idf

external_components:
  - source:
      type: local
      path: {COMPONENTS}
"""


def run_config(tmp_path: Path, base: str, block: str) -> tuple[bool, str]:
    """`esphome config` over one temporary file: did it pass, and what did it say."""
    path = tmp_path / "test.yaml"
    path.write_text(base + textwrap.dedent(block), encoding="utf-8")
    result = subprocess.run(
        [sys.executable, "-m", "esphome", "config", str(path)],
        capture_output=True,
        text=True,
        cwd=tmp_path,
    )
    output = result.stdout + result.stderr
    return "Configuration is valid" in output, output


# --- what it builds -----------------------------------------------------------


def test_a_pinned_display_gets_matrixs_own_controls(tmp_path):
    ok, output = run_config(
        tmp_path,
        DISPLAY,
        """
        wled_fx:
          id: fx
          display_id: matrix
          effect: Matrix
          controls: true
        """,
    )
    assert ok, output
    for name in ("Effect speed", "Spawning rate", "Trail", "Custom color", "Spawn"):
        assert f"name: {name}" in output, name
    # Matrix uses no palette, so there is no palette select and no select
    # platform in the build at all.
    assert "Color palette" not in output
    # And nothing generic survives.
    assert "Custom 1" not in output
    assert "Check 1" not in output


def test_a_display_always_gets_colour_one(tmp_path):
    # PS Fire paints from the palette and uses no colour slot, but colour 1 on
    # a display is also the panel's master brightness and its on/off.
    ok, output = run_config(
        tmp_path,
        DISPLAY,
        """
        wled_fx:
          id: fx
          display_id: matrix
          effect: PS Fire
          controls: true
        """,
    )
    assert ok, output
    assert "id: fx_color1" in output
    assert "name: Panel" in output


def test_the_light_front_end_leaves_colour_one_to_the_light(tmp_path):
    ok, output = run_config(
        tmp_path,
        STRIP,
        """
        wled_fx:

        light:
          - platform: esp32_rmt_led_strip
            id: strip
            name: Strip
            pin: GPIO16
            num_leds: 60
            rgb_order: GRB
            chipset: WS2812
            effects:
              - wled_fx:
                  id: strip_fx
                  name: Blink
                  effect: Blink
                  controls: true
        """,
    )
    assert ok, output
    # Blink uses colour 1 and colour 2. The light's own colour is colour 1
    # here, so only colour 2 becomes an entity of its own.
    assert "id: strip_fx_color1" not in output
    assert "id: strip_fx_color2" in output


def test_use_light_color_false_brings_colour_one_back(tmp_path):
    ok, output = run_config(
        tmp_path,
        STRIP,
        """
        wled_fx:

        light:
          - platform: esp32_rmt_led_strip
            id: strip
            name: Strip
            pin: GPIO16
            num_leds: 60
            rgb_order: GRB
            chipset: WS2812
            effects:
              - wled_fx:
                  id: strip_fx
                  name: Blink
                  effect: Blink
                  use_light_color: false
                  controls: true
        """,
    )
    assert ok, output
    assert "id: strip_fx_color1" in output


def test_the_mapping_form_renames_omits_and_prefixes(tmp_path):
    ok, output = run_config(
        tmp_path,
        DISPLAY,
        """
        wled_fx:
          id: fx
          display_id: matrix
          effect: Matrix
          controls:
            name_prefix: Rain
            entity_category: config
            speed:
              name: Fall speed
              icon: mdi:speedometer
            custom1: false
        """,
    )
    assert ok, output
    assert "name: Rain Fall speed" in output
    assert "icon: mdi:speedometer" in output
    assert "name: Rain Spawning rate" in output
    # custom1 was Trail, and it is gone. Colour 2 is also called Trail, so the
    # check has to be on the id rather than on the name.
    assert "id: fx_custom1" not in output
    assert "id: fx_color2" in output
    assert "entity_category: config" in output


def test_restore_value_reaches_the_entities(tmp_path):
    ok, output = run_config(
        tmp_path,
        DISPLAY,
        """
        wled_fx:
          id: fx
          display_id: matrix
          effect: PS Fire
          controls:
            restore_value: true
        """,
    )
    assert ok, output
    assert "restore_value: true" in output
    # PS Fire's first checkmark is on by default, so remembering it means the
    # switch's own restore mode has to come up on.
    assert "restore_mode: RESTORE_DEFAULT_ON" in output


# --- what it refuses ----------------------------------------------------------


def test_controls_without_a_pinned_effect(tmp_path):
    ok, output = run_config(
        tmp_path,
        DISPLAY,
        """
        wled_fx:
          id: fx
          display_id: matrix
          controls: true
        """,
    )
    assert not ok
    assert "exactly one 'effect:'" in output
    assert "generic" in output


def test_controls_beside_a_generic_platform_for_the_same_controller(tmp_path):
    ok, output = run_config(
        tmp_path,
        DISPLAY,
        """
        wled_fx:
          id: fx
          display_id: matrix
          effect: Matrix
          controls: true

        number:
          - platform: wled_fx
            wled_fx_id: fx
            type: custom1
            name: Custom 1
        """,
    )
    assert not ok
    assert "Pick one" in output
    assert "Matrix" in output


def test_controls_beside_an_allow_list_of_several_effects(tmp_path):
    ok, output = run_config(
        tmp_path,
        DISPLAY,
        """
        wled_fx:
          id: fx
          display_id: matrix
          effect: Matrix
          controls: true
          effects:
            - Matrix
            - Metaballs
            - PS Fire
        """,
    )
    assert not ok
    assert "allow-list" in output
    assert "'Metaballs'" in output


def test_an_override_naming_a_control_the_effect_does_not_use(tmp_path):
    ok, output = run_config(
        tmp_path,
        DISPLAY,
        """
        wled_fx:
          id: fx
          display_id: matrix
          effect: Matrix
          controls:
            custom2:
              name: Nope
        """,
    )
    assert not ok
    assert "does not use 'custom2'" in output
    # And it says what the effect does use, with WLED's name beside each slot.
    assert "intensity (Spawning rate)" in output


def test_restore_value_on_a_colour_slot(tmp_path):
    ok, output = run_config(
        tmp_path,
        DISPLAY,
        """
        wled_fx:
          id: fx
          display_id: matrix
          effect: Matrix
          controls:
            color2:
              restore_value: true
        """,
    )
    assert not ok
    assert "does not apply to a colour slot" in output


def test_controls_on_an_entry_that_drives_nothing(tmp_path):
    ok, output = run_config(
        tmp_path,
        STRIP,
        """
        wled_fx:
          effect: Blink
          controls: true
        """,
    )
    assert not ok
    assert "only applies to a wled_fx entry that drives a display" in output


# --- the allow-list -----------------------------------------------------------


class _Id:
    """Stands in for a resolved ESPHome ID, which is all these checks read."""

    def __init__(self, name):
        self.id = name


def _front_end(effect, *, controls=True, id_name="fx"):
    entry = {"display_id": _Id("matrix"), "id": _Id(id_name), "effect": effect}
    if controls:
        entry["controls"] = {"speed": {}}
    return entry


def test_a_pinned_build_compiles_in_only_its_own_effect():
    # No allow-list written, one front end, and it is pinned: nothing else
    # could ever be selected, so nothing else is worth the flash.
    entries = [_front_end("Matrix")]
    assert _check_named_controls({}, entries, []) == ["Matrix"]


def test_two_pinned_front_ends_compile_in_both():
    entries = [_front_end("Matrix"), _front_end("Metaballs", id_name="fx2")]
    assert _check_named_controls({}, entries, []) == ["Matrix", "Metaballs"]


def test_an_allow_list_naming_exactly_the_pinned_effect_is_fine():
    entries = [_front_end("Matrix")]
    assert _check_named_controls({}, entries, ["matrix"]) == ["matrix"]


def test_a_front_end_without_controls_leaves_the_allow_list_alone():
    entries = [_front_end("Matrix", controls=False)]
    assert _check_named_controls({}, entries, []) == []


def test_an_unpinned_front_end_beside_a_pinned_one_keeps_every_effect():
    # One output can still select anything, so narrowing the build to the
    # other one's effect would break it.
    entries = [_front_end("Matrix"), _front_end("Metaballs", controls=False, id_name="fx2")]
    assert _check_named_controls({}, entries, []) == []


@pytest.mark.parametrize("allow_list", [["Metaballs"], ["Matrix", "Metaballs"]])
def test_an_allow_list_that_is_not_the_pinned_effect_is_an_error(allow_list):
    import esphome.config_validation as cv

    entries = [_front_end("Matrix")]
    with pytest.raises(cv.Invalid, match="allow-list"):
        _check_named_controls({}, entries, allow_list)
