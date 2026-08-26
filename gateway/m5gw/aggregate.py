"""Turn the raw replies of eight backends into one small flat snapshot.

This module is pure — dicts in, dict out, no sockets, no clock (the caller
passes `now`).  That is what makes the interesting half of the gateway
testable on a laptop.

Design rules, both learned the hard way elsewhere in this house:

* A source that did not answer is *absent*, never zero and never a guess.
  It is listed in `err` so the remote can mark exactly those tiles stale
  instead of pretending the whole device is broken.
* Keys are short because the payload travels over Wi-Fi to a device without
  PSRAM, and every byte is radio time, which is battery.
"""

from typing import Any

# Hue groups worth showing on a remote: real rooms, not entertainment zones.
_ROOM_TYPE = "Room"


def _clean_name(name: str) -> str:
    """Hue names carry a leading dot to force sort order in the Hue app."""
    return (name or "").lstrip(".").strip()


def _num(v: Any, digits: int = 1):
    """Coerce to float and *deliberately* round to one decimal.

    Not laziness: the remote's display is 240x135 and shows "46.9 C", never
    "46.85 C", so the second decimal is bytes of radio time bought for
    nothing. Callers that want whole numbers pass digits=0.
    """
    try:
        return round(float(v), digits)
    except (TypeError, ValueError):
        return None


def hue_summary(groups: Any) -> dict | None:
    """{'g': [{'i','n','on','b'}...], 'on': <rooms currently lit>}"""
    if not isinstance(groups, dict):
        return None
    rooms = []
    for gid, g in groups.items():
        if not isinstance(g, dict) or g.get("type") != _ROOM_TYPE:
            continue
        state = g.get("state") or {}
        action = g.get("action") or {}
        try:
            i = int(gid)
        except (TypeError, ValueError):
            continue
        rooms.append({
            "i": i,
            "n": _clean_name(g.get("name", "")),
            "on": bool(state.get("any_on")),
            "b": int(action.get("bri") or 0),
        })
    rooms.sort(key=lambda r: r["i"])
    return {"g": rooms, "on": sum(1 for r in rooms if r["on"])}


def lichtwerk_summary(st: Any) -> dict | None:
    if not isinstance(st, dict):
        return None
    out = {"on": bool(st.get("power")), "b": int(st.get("brightness") or 0)}
    fx = st.get("effect")
    if fx:
        out["fx"] = str(fx)
    # Strip-warn owns the strip; the remote must not pretend it can paint.
    if st.get("strip_warn_mode"):
        out["warn"] = True
    return out


def teufel_summary(st: Any) -> dict | None:
    """Teufel is fire-and-forget IR; the Pi *estimates* this state.

    We pass `est: True` so the remote can render it as unconfirmed rather
    than claim knowledge nobody has.
    """
    if not isinstance(st, dict):
        return None
    return {
        "on": bool(st.get("powered")),
        "vol": int(st.get("volume") or 0),
        "mute": bool(st.get("muted")),
        "in": str(st.get("currentInput") or ""),
        "est": True,
    }


def fog_summary(status: Any, tank: Any) -> dict | None:
    if not isinstance(status, dict) and not isinstance(tank, dict):
        return None
    out: dict = {}
    if isinstance(status, dict):
        out["on"] = bool(status.get("fogActive"))
    if isinstance(tank, dict):
        pct = tank.get("level_pct")
        if pct is not None:
            out["tank"] = int(pct)
        ml = _num(tank.get("level_ml"), 0)
        if ml is not None:
            out["ml"] = int(ml)
    return out or None


def disco_summary(st: Any) -> dict | None:
    if not isinstance(st, dict):
        return None
    out = {
        "on": bool(st.get("active")),
        "bpm": int(_num(st.get("bpm"), 0) or 0),
        "spl": _num(st.get("spl")),
        "mode": str(st.get("mode") or ""),
    }
    return {k: v for k, v in out.items() if v is not None}


def clima_summary(indoor: Any, outdoor: Any) -> dict | None:
    def one(d):
        if not isinstance(d, dict):
            return None
        t, h = _num(d.get("temp")), _num(d.get("humidity"), 0)
        if t is None:
            return None
        o = {"t": t}
        if h is not None:
            o["h"] = int(h)
        age = d.get("age_seconds")
        # A sensor that stopped reporting is worse than no reading: it looks
        # current. Flag anything older than 10 minutes.
        if isinstance(age, (int, float)) and age > 600:
            o["old"] = int(age)
        return o

    inn, out = one(indoor), one(outdoor)
    if inn is None and out is None:
        return None
    res = {}
    if inn:
        res["in"] = inn
    if out:
        res["out"] = out
    return res


def weather_summary(wx: Any) -> dict | None:
    if not isinstance(wx, dict):
        return None
    cur = wx.get("current") or {}
    out = {"t": _num(cur.get("temp"))}
    if cur.get("icon"):
        out["ic"] = str(cur["icon"])
    if cur.get("desc"):
        out["d"] = str(cur["desc"])
    daily = wx.get("daily")
    if isinstance(daily, list) and daily:
        d0 = daily[0] or {}
        hi, lo = _num(d0.get("max"), 0), _num(d0.get("min"), 0)
        if hi is not None:
            out["hi"] = int(hi)
        if lo is not None:
            out["lo"] = int(lo)
    return out if out.get("t") is not None else None


def pi_summary(metrics: Any) -> dict | None:
    """raspi-monitor /api/metrics.

    Field names verified against a live reply, not guessed.  Note the house
    contract: every decimal arrives as a *string* ('46.85'), so everything
    goes through _num rather than being used directly.
    """
    if not isinstance(metrics, dict):
        return None

    def first(key):
        v = metrics.get(key)
        if isinstance(v, list):
            return v[0] if v else None
        return v if isinstance(v, dict) else None

    out: dict = {}
    cpu = first("cpu")
    if cpu:
        u = _num(cpu.get("cpu_usage_percent"))
        t = _num(cpu.get("cpu_temp_celsius"))
        if u is not None:
            out["cpu"] = u
        if t is not None:
            out["tmp"] = t
    mem = first("memory")
    if mem:
        m = _num(mem.get("usage_percent"))
        if m is not None:
            out["mem"] = m
    return out or None


#: Source key -> (builder, list of raw keys it consumes)
_BUILDERS = {
    "hue": (hue_summary, ("hue_groups",)),
    "lw": (lichtwerk_summary, ("lw",)),
    "yam": (lambda y: y or None, ("yam",)),
    "tf": (teufel_summary, ("tf",)),
    "fog": (fog_summary, ("fog", "tank")),
    "disco": (disco_summary, ("disco",)),
    "clima": (clima_summary, ("clima_in", "clima_out")),
    "wx": (weather_summary, ("wx",)),
    "pi": (pi_summary, ("pi",)),
}


def build_dash(raw: dict, now: int, fresh: set | None = None) -> dict:
    """raw maps source keys to decoded backend replies (or None on failure).

    `fresh` names the raw sources that were successfully polled this round.
    Anything present in `raw` but missing from `fresh` is a *last known*
    value: it still gets rendered, but is listed in `old` so the remote can
    dim it. That distinction is the whole point — a failed poll must never
    look like a broken device (the house rule that cost the dashboard a
    disappearing dB chart).
    """
    out: dict = {"t": int(now)}
    err, old = [], []
    for key, (fn, inputs) in _BUILDERS.items():
        try:
            val = fn(*(raw.get(i) for i in inputs))
        except Exception:            # a malformed backend must not 500 the gateway
            val = None
        if val:
            out[key] = val
            if fresh is not None and not any(i in fresh for i in inputs):
                old.append(key)
        else:
            err.append(key)
    if err:
        out["err"] = err
    if old:
        out["old"] = old
    return out
