"""The snapshot builder: small, honest, and never inventing a value."""

import json

from m5gw import aggregate as ag

# Shapes below are trimmed from live replies (raspi5, 2026-08-25/26).
HUE_GROUPS = {
    "200": {"name": "Musikbereich", "type": "Entertainment",
            "state": {"any_on": False}, "action": {"bri": 90, "on": False}},
    "201": {"name": "iLightShow", "type": "Entertainment",
            "state": {"any_on": False}, "action": {"on": False}},
    "81": {"name": ".Wohnzimmer", "type": "Room", "class": "Living room",
           "state": {"all_on": False, "any_on": True},
           "action": {"bri": 174, "on": True}},
    "83": {"name": "Küche", "type": "Room",
           "state": {"any_on": False}, "action": {"bri": 200, "on": False}},
}


def test_entertainment_zones_are_not_rooms():
    """A remote wants rooms; streaming zones are not switchable lights."""
    ids = [g["i"] for g in ag.hue_summary(HUE_GROUPS)["g"]]
    assert ids == [81, 83]


def test_leading_dot_is_stripped_from_hue_names():
    # Hue names start with '.' purely to force sort order in the Hue app;
    # on a 240 px display that dot is just noise.
    names = {g["i"]: g["n"] for g in ag.hue_summary(HUE_GROUPS)["g"]}
    assert names[81] == "Wohnzimmer"


def test_lit_room_count():
    assert ag.hue_summary(HUE_GROUPS)["on"] == 1


def test_teufel_state_is_flagged_as_an_estimate():
    """The Pi toggles a boolean after firing IR; nobody confirmed anything."""
    s = ag.teufel_summary({"powered": True, "volume": 29, "muted": True,
                           "currentInput": "AUX"})
    assert s["est"] is True


def test_decimal_strings_from_raspi_monitor_become_numbers():
    """House contract: the monitor sends decimals as strings ('46.85').

    Shipping those through as strings would make the remote parse text where
    it expects a number, so the coercion happens here, once.
    """
    s = ag.pi_summary({"cpu": [{"cpu_usage_percent": "2.71",
                                "cpu_temp_celsius": "46.85"}],
                       "memory": [{"usage_percent": "14.17"}]})
    assert all(isinstance(v, float) for v in s.values())
    assert s["tmp"] == 46.9


def test_readings_are_rounded_to_one_decimal_on_purpose():
    """A 240x135 display never shows a second decimal, so we do not send one."""
    s = ag.pi_summary({"cpu": [{"cpu_usage_percent": "2.7182818"}]})
    assert s["cpu"] == 2.7


def test_a_stale_sensor_is_marked_not_silently_shown_as_current():
    s = ag.clima_summary({"temp": 20.9, "humidity": 55, "age_seconds": 4000},
                         None)
    assert s["in"]["old"] == 4000


def test_a_fresh_sensor_carries_no_age_field():
    s = ag.clima_summary({"temp": 20.9, "humidity": 55, "age_seconds": 12},
                         None)
    assert "old" not in s["in"]


def test_failed_source_is_absent_and_listed_in_err():
    out = ag.build_dash({"lw": {"power": True, "brightness": 10}}, now=5)
    assert "yam" not in out and "yam" in out["err"]


def test_last_known_value_survives_a_failed_poll_and_is_marked_old():
    """The rule that cost the dashboard a chart: a failed poll is not 'no data'."""
    raw = {"lw": {"power": True, "brightness": 10},
           "tf": {"powered": False, "volume": 3, "muted": False,
                  "currentInput": "AUX"}}
    out = ag.build_dash(raw, now=5, fresh={"lw"})
    assert out["tf"]["vol"] == 3        # still rendered
    assert out["old"] == ["tf"]         # but honestly labelled
    assert "tf" not in out.get("err", [])


def test_fog_needs_only_one_of_its_two_sources():
    assert ag.fog_summary(None, {"level_pct": 48, "level_ml": 120.0})["tank"] == 48
    assert ag.fog_summary({"fogActive": True}, None)["on"] is True


def test_a_malformed_backend_cannot_take_down_the_snapshot():
    out = ag.build_dash({"hue_groups": "<html>500</html>",
                         "pi": ["unexpected"],
                         "lw": {"power": True, "brightness": 1}}, now=1)
    assert out["lw"]["on"] is True
    assert "hue" in out["err"] and "pi" in out["err"]


def test_snapshot_stays_under_the_one_kilobyte_budget():
    """The whole reason this gateway exists: /api/lights alone is 9565 bytes.

    Budget matters because the remote has no PSRAM and every byte is radio
    time, which is battery.
    """
    raw = {
        "hue_groups": {str(i): {"name": f".Raum{i}", "type": "Room",
                                "state": {"any_on": i % 2 == 0},
                                "action": {"bri": 180}} for i in range(81, 87)},
        "lw": {"power": True, "brightness": 255, "effect": "rainbow"},
        "yam": {"on": True, "vol": -28.0, "raw": -280, "mute": False,
                "in": "Spotify"},
        "tf": {"powered": True, "volume": 29, "muted": False,
               "currentInput": "AUX"},
        "fog": {"fogActive": False},
        "tank": {"level_pct": 48, "level_ml": 120.0},
        "disco": {"active": True, "bpm": 123.4, "spl": 62.5, "mode": "rainbow"},
        "clima_in": {"temp": 23.25, "humidity": 55.94, "age_seconds": 12},
        "clima_out": {"temp": 18.1, "humidity": 70.2, "age_seconds": 30},
        "wx": {"current": {"temp": 22.3, "icon": "03d", "desc": "Mäßig bewölkt"},
               "daily": [{"max": 22.3, "min": 20.3}]},
        "pi": {"cpu": [{"cpu_usage_percent": "2.71", "cpu_temp_celsius": "46.85"}],
               "memory": [{"usage_percent": "14.17"}]},
    }
    body = json.dumps(ag.build_dash(raw, now=1756180000),
                      separators=(",", ":"), ensure_ascii=False)
    assert len(body.encode()) < 1024, f"snapshot grew to {len(body.encode())} bytes"
