"""The wire contract between the device and the gateway, pinned from both ends.

The firmware serialises intents in core::buildActionBody; the gateway parses
them in m5gw.actions.plan. Neither side can run the other's code, so this test
lifts every JSON literal out of the firmware's ACTION_BODY_CONTRACT block and
feeds it to the real gateway parser. A drifted key name — the bug that once
sent a named value in the wrong field, making every input/effect/mode press
a 400 — now fails here instead of on the device.
"""

import json
import pathlib
import re
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "gateway"))

from m5gw import actions  # noqa: E402

FIRMWARE_TEST = ROOT / "firmware" / "test" / "test_core" / "test_main.cpp"

# What the gateway needs to know to resolve relative and stateful actions.
# Shaped like aggregate.build_dash() output.
STATE = {
    "hue": {"g": [{"i": 81, "n": "Wohnzimmer", "on": True, "b": 174},
                  {"i": 83, "n": "Kueche", "on": True, "b": 254}]},
    "lw": {"on": False, "b": 255, "fx": "solid"},
    "yam": {"on": True, "raw": -280, "mute": False, "in": "Spotify"},
    "tf": {"on": True, "vol": 29, "mute": True, "in": "AUX"},
    "fog": {"on": False},
    "disco": {"on": False, "mode": "rainbow"},
}


def contract_bodies():
    src = FIRMWARE_TEST.read_text()
    m = re.search(r"ACTION_BODY_CONTRACT_BEGIN(.*?)ACTION_BODY_CONTRACT_END",
                  src, re.S)
    assert m, "the ACTION_BODY_CONTRACT block is gone from the firmware tests"
    block = m.group(1)
    # Every expected wire body is a C string literal starting with "{".
    literals = re.findall(r'TEST_ASSERT_EQUAL_STRING\("(\{.*?\})",\s*out\)', block)
    assert literals, "no bodies found in the contract block"
    return [json.loads(lit.replace('\\"', '"')) for lit in literals]


BODIES = contract_bodies()


@pytest.mark.parametrize("body", BODIES, ids=[f"{b['target']}.{b['action']}" for b in BODIES])
def test_every_firmware_body_is_accepted_by_the_gateway(body):
    target, action = body["target"], body["action"]
    if target == "macro":
        plans = actions.macro(action, STATE)
    else:
        plans = [actions.plan(target, action, body, STATE)]
    assert plans, f"{target}.{action} produced no plan"
    for p in plans:
        assert p.backend and p.path, f"{target}.{action}: plan lacks a route"


def test_the_contract_block_covers_every_target_the_device_can_send():
    """A target that the firmware knows but the block does not is a contract
    with no witness on the gateway side."""
    seen = {b["target"] for b in BODIES}
    assert seen >= {"hue", "lw", "yam", "tf", "disco", "fog", "macro"}, seen


def test_a_drifted_key_name_is_rejected_here_not_on_the_device():
    """The reason this file exists: the literal parameter names matter."""
    with pytest.raises(actions.ActionError):
        actions.plan("yam", "input", {"target": "yam", "action": "input",
                                       "name": "Spotify"}, STATE)
    with pytest.raises(actions.ActionError):
        actions.plan("fog", "on", {"target": "fog", "action": "on"}, STATE)
