"""The receiver's XML is the one wire format we have to speak ourselves."""

import pytest

from m5gw import yamaha_xml as yx

# Trimmed from a real Basic_Status reply of the RX-V577 (2026-08-25).
REAL = (
    '<YAMAHA_AV rsp="GET" RC="0"><Main_Zone><Basic_Status>'
    "<Power_Control><Power>On</Power><Zone_B_Power_Info>Standby</Zone_B_Power_Info>"
    "<Sleep>Off</Sleep></Power_Control>"
    "<Volume><Lvl><Val>-280</Val><Exp>1</Exp><Unit>dB</Unit></Lvl><Mute>Off</Mute>"
    "<Subwoofer_Trim><Val>0</Val><Exp>1</Exp><Unit>dB</Unit></Subwoofer_Trim>"
    "<Scale>dB</Scale>"
    # Zone_B nests *inside* Volume and carries its own Lvl and Mute. This is
    # the trap: a naive "last match" or a document-wide search reads Zone B.
    "<Zone_B><Feature_Availability>Ready</Feature_Availability><Interlock>On</Interlock>"
    "<Lvl><Val>-999</Val><Exp>1</Exp><Unit>dB</Unit></Lvl><Mute>On</Mute></Zone_B>"
    "</Volume>"
    "<Input><Input_Sel>Spotify</Input_Sel><Input_Sel_Item_Info>"
    "<Param>Spotify</Param><Title> Spotify </Title></Input_Sel_Item_Info></Input>"
    "</Basic_Status></Main_Zone></YAMAHA_AV>"
)


def test_parses_the_five_fields_a_remote_needs():
    s = yx.parse_status(REAL)
    assert s["on"] is True
    assert s["vol"] == -28.0          # Val=-280 with Exp=1
    assert s["raw"] == -280
    assert s["mute"] is False
    assert s["in"] == "Spotify"


def test_main_zone_wins_over_zone_b():
    """Zone_B nests inside Volume with its own Lvl/Mute.

    Reading those would report the wrong volume and an inverted mute icon,
    and it would look plausible — which is why it gets its own test.
    """
    s = yx.parse_status(REAL)
    assert s["raw"] == -280 and s["mute"] is False    # not -999 / not True


def test_error_code_yields_nothing_rather_than_half_a_reading():
    # The receiver answers HTTP 200 even when it refuses a command; RC is the
    # real verdict. (Same trap that hid a broken Extra-Bass call for years.)
    assert yx.parse_status(REAL.replace('RC="0"', 'RC="3"')) == {}


@pytest.mark.parametrize("junk", ["", "not xml", "<html>404</html>", None])
def test_junk_input_is_empty_not_an_exception(junk):
    assert yx.parse_status(junk) == {}


def test_missing_fields_are_absent_never_defaulted():
    xml = '<YAMAHA_AV rsp="GET" RC="0"><Main_Zone></Main_Zone></YAMAHA_AV>'
    assert yx.parse_status(xml) == {}


def test_volume_steps_are_half_decibels():
    assert yx.step_raw(-280, 1) == -275     # -28.0 -> -27.5 dB
    assert yx.step_raw(-280, -4) == -300


def test_volume_is_clamped_to_the_receivers_range():
    assert yx.step_raw(160, 40) == yx.VOL_MAX_RAW
    assert yx.step_raw(-800, -40) == yx.VOL_MIN_RAW


def test_power_request_rejects_a_bogus_state():
    with pytest.raises(ValueError):
        yx.power_request("Sleep")


def test_input_request_rejects_injection():
    with pytest.raises(ValueError):
        yx.input_request("AUX</Input_Sel><Power>On</Power><Input_Sel>")


def test_input_request_accepts_the_odd_real_name_with_parentheses():
    assert "<Input_Sel>iPod (USB)</Input_Sel>" in yx.input_request("iPod (USB)")
