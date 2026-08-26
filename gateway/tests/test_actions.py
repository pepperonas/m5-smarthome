"""Write actions: clamps, interlocks, and the refusal to guess."""

import pytest

from m5gw import actions
from m5gw.actions import ActionError

STATE = {
    "hue": {"g": [{"i": 81, "n": "Wohnzimmer", "on": True, "b": 174},
                  {"i": 83, "n": "Küche", "on": False, "b": 200}], "on": 1},
    "lw": {"on": False, "b": 255},
    "yam": {"on": True, "vol": -28.0, "raw": -280, "mute": False},
    "disco": {"on": False},
}


# --- the safety interlock -------------------------------------------------

def test_fog_refuses_to_ignite_without_an_explicit_confirmation():
    """220 V and a heating element: no single stray byte may start this."""
    with pytest.raises(ActionError) as e:
        actions.plan("fog", "on", {}, STATE)
    assert e.value.status == 409


def test_fog_confirmation_must_be_true_not_merely_present():
    for bogus in ("yes", 1, "true", None, {}):
        with pytest.raises(ActionError):
            actions.plan("fog", "on", {"confirm": bogus}, STATE)


def test_fog_ignites_only_with_confirm_true():
    p = actions.plan("fog", "on", {"confirm": True}, STATE)
    assert (p.method, p.path) == ("POST", "/api/fog/on")


def test_fog_off_is_never_gated():
    """Stopping a heater must work even when nothing else does."""
    p = actions.plan("fog", "off", {}, None)
    assert p.path == "/api/fog/off"


def test_fog_has_no_toggle():
    """With a fogger you always say which direction you mean."""
    with pytest.raises(ActionError):
        actions.plan("fog", "toggle", {"confirm": True}, STATE)


# --- never guess ----------------------------------------------------------

def test_toggle_without_known_state_refuses_rather_than_flipping_a_coin():
    with pytest.raises(ActionError) as e:
        actions.plan("lw", "toggle", {}, None)
    assert e.value.status == 409


def test_toggle_uses_the_cached_state():
    assert actions.plan("lw", "toggle", {}, STATE).json == {"power": True}
    assert actions.plan("hue", "toggle", {"group": 81}, STATE).json == {"on": False}
    assert actions.plan("hue", "toggle", {"group": 83}, STATE).json == {"on": True}


def test_volume_step_without_a_known_level_refuses():
    with pytest.raises(ActionError) as e:
        actions.plan("yam", "vol", {"step": 2}, {"yam": {"on": True}})
    assert e.value.status == 409


# --- clamps and whitelists -------------------------------------------------

def test_yamaha_step_is_half_decibels_from_the_current_level():
    p = actions.plan("yam", "vol", {"step": 2}, STATE)
    assert "<Val>-270</Val>" in p.data          # -28.0 dB + 2 * 0.5 dB
    assert p.optimistic["yam"]["vol"] == -27.0


def test_brightness_out_of_range_is_rejected_not_clamped_silently():
    with pytest.raises(ActionError):
        actions.plan("hue", "bri", {"group": 81, "bri": 500}, STATE)
    with pytest.raises(ActionError):
        actions.plan("hue", "bri", {"group": 81, "bri": 0}, STATE)


def test_unknown_effect_is_refused():
    with pytest.raises(ActionError):
        actions.plan("lw", "effect", {"effect": "disco_inferno"}, STATE)


def test_iris_warn_is_not_offered_to_the_remote():
    """It belongs to the disco strip-warn path; the remote would fight it."""
    with pytest.raises(ActionError):
        actions.plan("lw", "effect", {"effect": "iris_warn"}, STATE)


def test_effect_whitelist_matches_the_controller():
    # Mirrors valid_effects in lichtwerk-controller/web_controller.py.
    for fx in ("solid", "rainbow", "pulse", "chase", "sparkle", "strobe",
               "meteor", "breathe", "sinelon", "juggle", "theater",
               "gradient", "fire"):
        assert actions.plan("lw", "effect", {"effect": fx}, STATE).json["effect"] == fx


def test_unknown_yamaha_input_is_refused():
    with pytest.raises(ActionError):
        actions.plan("yam", "input", {"input": "Bluetooth"}, STATE)   # not on this unit


def test_unknown_target_is_404_not_500():
    with pytest.raises(ActionError) as e:
        actions.plan("nope", "on", {}, STATE)
    assert e.value.status == 404


def test_no_generic_passthrough_exists():
    """A path the remote controls would be a hole into six open backends."""
    for hostile in ("../../etc/passwd", "/api/global/emergency-off", "raw"):
        with pytest.raises(ActionError):
            actions.plan("hue", hostile, {"path": hostile}, STATE)


# --- macros ---------------------------------------------------------------

def test_goodnight_switches_the_fog_machine_off_and_never_on():
    plans = actions.macro("goodnight", STATE)
    paths = [p.path for p in plans]
    assert "/api/fog/off" in paths
    assert not any("fog/on" in p for p in paths)


def test_goodnight_skips_a_receiver_that_is_already_off():
    plans = actions.macro("goodnight", {**STATE, "yam": {"on": False}})
    assert not any(p.backend == "yam" for p in plans)
    assert any(p.backend == "yam" for p in actions.macro("goodnight", STATE))


def test_wake_does_not_start_the_fog_machine_either():
    assert not any(p.backend == "fog" for p in actions.macro("wake", STATE))
