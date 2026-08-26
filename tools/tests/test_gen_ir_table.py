"""The IR table generator.

The Teufel codes were captured once from the original remote and live in one
CSV. This generator renders them into a C++ header rather than anyone
hand-copying them, because a drifted IR table fails *silently*: the LED
blinks, the amplifier ignores it, and nothing reports an error.
"""

import importlib.util
import pathlib
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("gen_ir_table",
                                              ROOT / "tools" / "gen_ir_table.py")
gen = importlib.util.module_from_spec(spec)
sys.modules["gen_ir_table"] = gen
spec.loader.exec_module(gen)


def test_parses_the_canonical_csv():
    rows = gen.parse(gen.CSV.read_text())
    assert len(rows) == 19
    assert ("Power", 0x48) in rows
    assert ("Vol_Up", 0xB0) in rows


def test_blank_lines_and_comments_are_ignored():
    rows = gen.parse("# a note\n0x48;Power\n\n   \n0x40;Bluetooth\n")
    assert rows == [("Power", 0x48), ("Bluetooth", 0x40)]


def test_a_malformed_row_stops_generation():
    """Half a table is worse than none: it would ship codes that look right."""
    with pytest.raises(SystemExit):
        gen.parse("0x48\n")
    with pytest.raises(SystemExit):
        gen.parse("")


def test_a_bad_hex_value_is_not_silently_zero():
    with pytest.raises(ValueError):
        gen.parse("0xZZ;Power\n")


def test_the_digest_changes_when_the_source_does():
    a = gen.csum("0x48;Power\n")
    assert a != gen.csum("0x49;Power\n")
    assert a == gen.csum("0x48;Power\n")          # and is stable


def test_generated_header_carries_names_codes_and_provenance():
    out = gen.render([("Power", 0x48), ("Vol_Up", 0xB0)], "deadbeef")
    assert "IR_POWER = 0x48" in out
    assert "IR_VOL_UP = 0xB0" in out
    assert '{"power", 0x48}' in out               # lower-case, for the console
    assert "deadbeef" in out                      # traceable to its input
    assert "GENERATED" in out and "do not edit" in out
    assert "0x5780" in out                        # the verified device address


def test_the_committed_header_matches_the_csv():
    """Drift check, the same one CI runs: a stale table cannot ship."""
    current = gen.OUT.read_text()
    expected = gen.render(gen.parse(gen.CSV.read_text()),
                          gen.csum(gen.CSV.read_text()))
    assert current == expected


def test_the_known_bad_byte_stays_documented():
    # MUTE (0x28) reaches the box and does nothing. Undocumenting it would
    # cost the next person an evening.
    out = gen.render([("Mute", 0x28)], "x")
    assert "MUTE (0x28)" in out and "does nothing" in out
