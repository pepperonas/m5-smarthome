"""The control table is the contract between the core, the renderer and
the documentation. It is generated from one JSON; these pin the JSON's
shape and the generator's output."""

import json
import pathlib
import subprocess
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import gen_controls  # noqa: E402

TABLE = json.loads((ROOT / "firmware" / "controls.json").read_text())
SCREENS = TABLE["screens"]


def all_controls():
    for screen, controls in SCREENS.items():
        for c in controls:
            yield screen, c


def test_every_kind_is_one_of_the_nine():
    for screen, c in all_controls():
        assert c["kind"] in TABLE["kinds"], f"{screen}: unknown kind {c['kind']!r}"


def test_levels_carry_bounds_and_a_positive_step():
    """A Level needs bounds, a positive step and a known format. The bounds
    are not required to divide evenly by the step — RoomBri (1..254 step 30)
    and LwBri (0..255 step 32) come straight from the Hue and Lichtwerk APIs
    and deliberately do not; clamping handles the last, short step."""
    for screen, c in all_controls():
        if c["kind"] != "Level":
            continue
        for k in ("min", "max", "step", "fmt"):
            assert k in c, f"{screen}/{c['label']}: Level without {k}"
        assert c["max"] > c["min"]
        assert c["step"] > 0
        assert c["fmt"] in TABLE["fmts"]


def test_labels_are_ascii_and_fit_a_row():
    """Font0 is 6 px per character; the label column is 14 characters."""
    for screen, c in all_controls():
        label = c["label"]
        assert label.isascii(), f"{screen}: {label!r} is not ASCII"
        assert len(label) <= 14, f"{screen}: {label!r} longer than 14"


def test_accelerators_are_unique_per_screen_and_never_a_grammar_key():
    grammar = set(";.,/+-`1234567")
    for screen, controls in SCREENS.items():
        keys = [c["key"] for c in controls if c.get("key")]
        assert len(keys) == len(set(keys)), f"{screen}: duplicate accelerator"
        for k in keys:
            assert len(k) == 1 and k not in grammar, f"{screen}: {k!r} collides"


def test_binds_are_unique_across_all_screens():
    binds = [c["bind"] for _, c in all_controls()]
    assert len(binds) == len(set(binds)), "a Bind name is reused"


def test_the_generated_header_is_current():
    """The header is committed; regenerating must change nothing."""
    header = ROOT / "firmware" / "lib" / "core" / "controls_table.h"
    assert header.read_text() == gen_controls.render_header(TABLE)


def test_the_generated_doc_is_current():
    doc = ROOT / "docs" / "CONTROLS.md"
    assert doc.read_text() == gen_controls.render_doc(TABLE)


def test_check_mode_fails_on_drift(tmp_path, monkeypatch):
    header = ROOT / "firmware" / "lib" / "core" / "controls_table.h"
    original = header.read_text()
    try:
        header.write_text(original + "\n// drift\n")
        r = subprocess.run([sys.executable, "tools/gen_controls.py", "--check"],
                           cwd=ROOT, capture_output=True, text=True)
        assert r.returncode != 0, "drift went unnoticed"
    finally:
        header.write_text(original)
