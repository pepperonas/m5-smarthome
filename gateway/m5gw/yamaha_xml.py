"""Yamaha RX-V577 XML helpers.

The yamaha-controller on the Pi is a *transparent proxy*: it forwards
YamahaRemoteControl XML to the receiver and returns the receiver's XML
verbatim.  There is no JSON status endpoint (verified 2026-08-25:
GET :5001/api/status -> 404).  So somebody has to parse XML, and it is
much cheaper to do it here than on a microcontroller without PSRAM.

Everything in this module is pure: string in, data out.  No I/O.
"""

import re

ZONE_MAIN = "Main_Zone"

# The receiver reports volume as a scaled integer: Val=-280, Exp=1 -> -28.0 dB.
# Its step size is 5 (= 0.5 dB).
VOL_STEP_RAW = 5
VOL_MIN_RAW = -805
VOL_MAX_RAW = 165


def status_request(zone: str = ZONE_MAIN) -> str:
    return f'<YAMAHA_AV cmd="GET"><{zone}><Basic_Status>GetParam</Basic_Status></{zone}></YAMAHA_AV>'


def power_request(state: str, zone: str = ZONE_MAIN) -> str:
    """state: 'On' or 'Standby'."""
    if state not in ("On", "Standby"):
        raise ValueError(f"bad power state: {state!r}")
    return (f'<YAMAHA_AV cmd="PUT"><{zone}><Power_Control><Power>{state}</Power>'
            f"</Power_Control></{zone}></YAMAHA_AV>")


def volume_request(raw: int, zone: str = ZONE_MAIN) -> str:
    """raw is the receiver's scaled value (-280 == -28.0 dB)."""
    raw = clamp_raw(raw)
    return (f'<YAMAHA_AV cmd="PUT"><{zone}><Volume><Lvl><Val>{raw}</Val>'
            f"<Exp>1</Exp><Unit>dB</Unit></Lvl></Volume></{zone}></YAMAHA_AV>")


def mute_request(state: str, zone: str = ZONE_MAIN) -> str:
    if state not in ("On", "Off"):
        raise ValueError(f"bad mute state: {state!r}")
    return (f'<YAMAHA_AV cmd="PUT"><{zone}><Volume><Mute>{state}</Mute>'
            f"</Volume></{zone}></YAMAHA_AV>")


def input_request(name: str, zone: str = ZONE_MAIN) -> str:
    if not re.fullmatch(r"[A-Za-z0-9_ ().-]{1,24}", name or ""):
        raise ValueError(f"bad input name: {name!r}")
    return (f'<YAMAHA_AV cmd="PUT"><{zone}><Input><Input_Sel>{name}</Input_Sel>'
            f"</Input></{zone}></YAMAHA_AV>")


def clamp_raw(raw: int) -> int:
    return max(VOL_MIN_RAW, min(VOL_MAX_RAW, int(raw)))


def step_raw(raw: int, steps: int) -> int:
    """Move the volume by `steps` half-decibel notches."""
    return clamp_raw(int(raw) + int(steps) * VOL_STEP_RAW)


def _tag(xml: str, name: str) -> str | None:
    """First <name>…</name> body, or None.

    Deliberately not a real XML parser: the receiver's reply is a fixed,
    machine-generated shape and we only need five leaf values out of ~4 KB.
    """
    m = re.search(rf"<{name}>(.*?)</{name}>", xml, re.S)
    return m.group(1) if m else None


def parse_status(xml: str) -> dict:
    """Extract the handful of fields the remote actually shows.

    Returns {} for anything unparseable — the caller decides what a missing
    source means (see aggregate.build_dash), we never invent values.
    """
    if not xml or "<YAMAHA_AV" not in xml:
        return {}
    rc = re.search(r'RC="(\d+)"', xml)
    if rc and rc.group(1) != "0":
        return {}

    out: dict = {}

    power = _tag(_tag(xml, "Power_Control") or "", "Power")
    if power:
        out["on"] = power == "On"

    vol_block = _tag(xml, "Volume") or ""
    lvl = _tag(vol_block, "Lvl") or ""
    val, exp = _tag(lvl, "Val"), _tag(lvl, "Exp")
    if val is not None:
        try:
            raw = int(val)
            out["raw"] = raw
            out["vol"] = round(raw / (10 ** int(exp or 1)), 1)
        except ValueError:
            pass
    mute = _tag(vol_block, "Mute")
    if mute:
        out["mute"] = mute == "On"

    sel = _tag(_tag(xml, "Input") or "", "Input_Sel")
    if sel:
        out["in"] = sel.strip()

    return out
