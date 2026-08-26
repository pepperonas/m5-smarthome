"""Named write actions.

Deliberately *not* a generic pass-through.  A proxy that forwards arbitrary
paths to six unauthenticated loopback services is a hole punched straight
through them; a fixed table of named actions is a door with a frame.

Pure module: `plan()` decides *what* request to make, it never makes one.
The caller (app.py) executes the returned Plan.  That keeps every routing
rule, every clamp and the fog safety interlock testable without a network.
"""

from dataclasses import dataclass, field
from typing import Any

from . import yamaha_xml


class ActionError(Exception):
    """Bad request from the remote — carries the HTTP status to answer with."""

    def __init__(self, message: str, status: int = 400):
        super().__init__(message)
        self.message = message
        self.status = status


@dataclass
class Plan:
    backend: str                       # key into config.BACKENDS
    method: str
    path: str
    json: Any = None                   # JSON body, or None
    data: str | None = None            # raw body (Yamaha speaks XML)
    headers: dict = field(default_factory=dict)
    #: what the remote may optimistically assume, e.g. {"lw": {"on": True}}
    optimistic: dict = field(default_factory=dict)


def _int(params: dict, key: str, lo: int, hi: int, default=None) -> int:
    v = params.get(key, default)
    if v is None:
        raise ActionError(f"missing parameter: {key}")
    try:
        v = int(v)
    except (TypeError, ValueError):
        raise ActionError(f"{key} must be an integer, got {v!r}")
    if not lo <= v <= hi:
        raise ActionError(f"{key} out of range {lo}..{hi}: {v}")
    return v


def _state_path(state: dict | None, *keys, default=None):
    node: Any = state or {}
    for k in keys:
        if not isinstance(node, dict) or k not in node:
            return default
        node = node[k]
    return node


def _resolve_toggle(current: bool | None, action: str) -> bool:
    """'on'/'off' are absolute; 'toggle' needs to know where we are."""
    if action == "on":
        return True
    if action == "off":
        return False
    if current is None:
        # Never guess. A remote that flips the wrong way is worse than one
        # that says "I don't know yet".
        raise ActionError("state unknown, cannot toggle", status=409)
    return not current


# --------------------------------------------------------------------------
# Hue
# --------------------------------------------------------------------------

def _hue(action: str, p: dict, state: dict | None) -> Plan:
    if action in ("on", "off", "toggle"):
        gid = _int(p, "group", 1, 999)
        rooms = _state_path(state, "hue", "g", default=[]) or []
        cur = next((r.get("on") for r in rooms if r.get("i") == gid), None)
        want = _resolve_toggle(cur, action)
        return Plan("hue", "PUT", f"/api/groups/{gid}/action", json={"on": want},
                    optimistic={"hue": {"group": gid, "on": want}})

    if action == "bri":
        gid = _int(p, "group", 1, 999)
        bri = _int(p, "bri", 1, 254)
        # hue-controller auto-activates a dark group when brightness is set.
        return Plan("hue", "PUT", f"/api/groups/{gid}/action", json={"bri": bri},
                    optimistic={"hue": {"group": gid, "on": True, "b": bri}})

    if action == "ct":
        gid = _int(p, "group", 1, 999)
        ct = _int(p, "ct", 153, 500)
        return Plan("hue", "PUT", f"/api/groups/{gid}/action",
                    json={"on": True, "ct": ct},
                    optimistic={"hue": {"group": gid, "on": True}})

    if action == "all":
        want = _resolve_toggle(None if p.get("on") is None else bool(p["on"]),
                               "on" if p.get("on") else "off")
        return Plan("hue", "PUT", "/api/global/all-lights", json={"on": want},
                    optimistic={"hue": {"all": want}})

    raise ActionError(f"unknown hue action: {action!r}")


# --------------------------------------------------------------------------
# Lichtwerk (LED strip)
# --------------------------------------------------------------------------

_LW_EFFECTS = {
    # Verified against lichtwerk-controller/web_controller.py valid_effects.
    # 'iris_warn' is deliberately absent: it belongs to the disco strip-warn
    # path, and a remote painting it would fight the audio engine for the strip.
    "solid", "rainbow", "pulse", "chase", "sparkle", "strobe", "meteor",
    "breathe", "sinelon", "juggle", "theater", "gradient", "fire",
}


def _lichtwerk(action: str, p: dict, state: dict | None) -> Plan:
    if action in ("on", "off", "toggle"):
        cur = _state_path(state, "lw", "on")
        want = _resolve_toggle(cur, action)
        return Plan("lw", "POST", "/api/power", json={"power": want},
                    optimistic={"lw": {"on": want}})

    if action == "bri":
        bri = _int(p, "bri", 0, 255)
        return Plan("lw", "POST", "/api/brightness", json={"brightness": bri},
                    optimistic={"lw": {"b": bri}})

    if action == "speed":
        return Plan("lw", "POST", "/api/speed",
                    json={"speed": _int(p, "speed", 1, 100)})

    if action == "effect":
        fx = str(p.get("effect") or "")
        if fx not in _LW_EFFECTS:
            raise ActionError(f"unknown effect: {fx!r}")
        return Plan("lw", "POST", "/api/effect", json={"effect": fx},
                    optimistic={"lw": {"fx": fx, "on": True}})

    if action == "color":
        rgb = {c: _int(p, c, 0, 255) for c in ("r", "g", "b")}
        return Plan("lw", "POST", "/api/color", json=rgb)

    raise ActionError(f"unknown lichtwerk action: {action!r}")


# --------------------------------------------------------------------------
# Yamaha RX-V577 (XML over the controller's transparent proxy)
# --------------------------------------------------------------------------

_YAM_PATH = "/api/receiver/YamahaRemoteControl/ctrl"
_YAM_HEADERS = {"Content-Type": "text/xml; charset=UTF-8"}
_YAM_INPUTS = {
    # Queried live from the receiver (Input_Sel_Item), not guessed:
    # this RX-V577 reports exactly these names and rejects anything else.
    "Spotify", "JUKE", "AirPlay", "SERVER", "NET RADIO", "USB", "iPod (USB)",
    "TUNER", "AUX",
    "HDMI1", "HDMI2", "HDMI3", "HDMI4", "HDMI5", "HDMI6",
    "AV1", "AV2", "AV3", "AV4", "AV5", "AV6",
}


def _yamaha(action: str, p: dict, state: dict | None) -> Plan:
    def xml(body: str, opt: dict) -> Plan:
        return Plan("yam", "POST", _YAM_PATH, data=body,
                    headers=dict(_YAM_HEADERS), optimistic={"yam": opt})

    if action in ("on", "off", "toggle"):
        cur = _state_path(state, "yam", "on")
        want = _resolve_toggle(cur, action)
        return xml(yamaha_xml.power_request("On" if want else "Standby"),
                   {"on": want})

    if action == "vol":
        # Relative steps are what a remote actually wants; absolute dB exists
        # for the rare "set it to exactly this" case.
        if "step" in p:
            raw = _state_path(state, "yam", "raw")
            if raw is None:
                raise ActionError("volume unknown, cannot step", status=409)
            steps = _int(p, "step", -40, 40)
            new = yamaha_xml.step_raw(raw, steps)
        elif "db" in p:
            try:
                new = yamaha_xml.clamp_raw(round(float(p["db"]) * 10))
            except (TypeError, ValueError):
                raise ActionError("db must be a number")
        else:
            raise ActionError("vol needs 'step' or 'db'")
        return xml(yamaha_xml.volume_request(new),
                   {"raw": new, "vol": round(new / 10, 1)})

    if action == "mute":
        cur = _state_path(state, "yam", "mute")
        want = _resolve_toggle(cur, "toggle" if p.get("on") is None
                               else ("on" if p["on"] else "off"))
        return xml(yamaha_xml.mute_request("On" if want else "Off"),
                   {"mute": want})

    if action == "input":
        name = str(p.get("input") or "")
        if name not in _YAM_INPUTS:
            raise ActionError(f"unknown input: {name!r}")
        return xml(yamaha_xml.input_request(name), {"in": name})

    raise ActionError(f"unknown yamaha action: {action!r}")


# --------------------------------------------------------------------------
# Teufel / PowerHiFi (the Pi drives an IR bridge; state is an estimate)
# --------------------------------------------------------------------------

_TF_INPUTS = {"AUX", "LINE", "OPTICAL", "USB", "BLUETOOTH"}


def _teufel(action: str, p: dict, state: dict | None) -> Plan:
    if action == "power":
        # The box only understands "toggle"; there is no absolute power command.
        return Plan("tf", "POST", "/api/power", json={},
                    optimistic={"tf": {"toggle_power": True}})

    if action == "vol":
        step = _int(p, "step", -20, 20)
        if step == 0:
            raise ActionError("step must not be 0")
        return Plan("tf", "POST", "/api/volume",
                    json={"action": "up" if step > 0 else "down",
                          "level": abs(step)},
                    optimistic={"tf": {"dvol": step}})

    if action == "mute":
        # Known house quirk: CMD_MUTE (0x28) reaches the box and does nothing.
        # We still expose it — the fault is in the captured byte, not here.
        return Plan("tf", "POST", "/api/mute", json={},
                    optimistic={"tf": {"toggle_mute": True}})

    if action == "input":
        name = str(p.get("input") or "").upper()
        if name not in _TF_INPUTS:
            raise ActionError(f"unknown input: {name!r}")
        return Plan("tf", "POST", "/api/input", json={"input": name},
                    optimistic={"tf": {"in": name}})

    raise ActionError(f"unknown teufel action: {action!r}")


# --------------------------------------------------------------------------
# Fog machine — 220 V and hot. Interlocked on purpose.
# --------------------------------------------------------------------------

def _fog(action: str, p: dict, state: dict | None) -> Plan:
    if action == "off":
        # Stopping is always safe and must never be gated behind anything.
        return Plan("fog", "POST", "/api/fog/off", json={},
                    optimistic={"fog": {"on": False}})

    if action == "on":
        if p.get("confirm") is not True:
            raise ActionError(
                "fog requires an explicit confirm:true — no accidental ignition",
                status=409)
        return Plan("fog", "POST", "/api/fog/on", json={},
                    optimistic={"fog": {"on": True}})

    # There is deliberately no 'toggle': with a heater you always say which
    # direction you mean.
    raise ActionError(f"unknown fog action: {action!r} (use on/off)")


# --------------------------------------------------------------------------
# Disco
# --------------------------------------------------------------------------

_DISCO_MODES = {"rainbow", "party", "random", "pulse", "solid", "strobe"}


def _disco(action: str, p: dict, state: dict | None) -> Plan:
    if action in ("on", "off", "toggle"):
        cur = _state_path(state, "disco", "on")
        want = _resolve_toggle(cur, action)
        return Plan("disco", "POST", "/api/start" if want else "/api/stop",
                    json={}, optimistic={"disco": {"on": want}})

    if action == "mode":
        mode = str(p.get("mode") or "")
        if mode not in _DISCO_MODES:
            raise ActionError(f"unknown disco mode: {mode!r}")
        return Plan("disco", "POST", "/api/config", json={"mode": mode},
                    optimistic={"disco": {"mode": mode}})

    if action == "bri":
        return Plan("disco", "POST", "/api/config",
                    json={"brightness": _int(p, "bri", 10, 100)})

    raise ActionError(f"unknown disco action: {action!r}")


_TARGETS = {
    "hue": _hue,
    "lw": _lichtwerk,
    "yam": _yamaha,
    "tf": _teufel,
    "fog": _fog,
    "disco": _disco,
}


def plan(target: str, action: str, params: dict | None = None,
         state: dict | None = None) -> Plan:
    """Translate one named action into one backend request.

    Raises ActionError (with .status) for anything the remote got wrong.
    """
    fn = _TARGETS.get(target)
    if fn is None:
        raise ActionError(f"unknown target: {target!r}", status=404)
    if not action:
        raise ActionError("missing action")
    return fn(action, params or {}, state)


def macro(name: str, state: dict | None = None) -> list[Plan]:
    """Multi-device shortcuts. One key press, one request, several backends.

    Fog is never started by a macro and is switched *off* by 'alloff' —
    a blanket "everything off" that leaves a fogger running would be a trap.
    """
    if name in ("alloff", "goodnight"):
        plans = [
            Plan("hue", "PUT", "/api/global/all-lights", json={"on": False},
                 optimistic={"hue": {"all": False}}),
            Plan("lw", "POST", "/api/power", json={"power": False},
                 optimistic={"lw": {"on": False}}),
            Plan("disco", "POST", "/api/stop", json={},
                 optimistic={"disco": {"on": False}}),
            Plan("fog", "POST", "/api/fog/off", json={},
                 optimistic={"fog": {"on": False}}),
        ]
        if _state_path(state, "yam", "on"):
            plans.append(_yamaha("off", {}, state))
        return plans

    if name in ("allon", "wake"):
        return [
            Plan("hue", "PUT", "/api/global/all-lights", json={"on": True},
                 optimistic={"hue": {"all": True}}),
        ]

    raise ActionError(f"unknown macro: {name!r}", status=404)
