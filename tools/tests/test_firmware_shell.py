"""Contract pins on the Arduino shell.

The shell has no host tests — it needs real hardware — which is exactly why
the worst bug of this project lived there: the vendor's isChange() is
*consuming*, and the shell called it twice per loop, so every key press was
detected and then read back as empty. Nothing responded to any key, on any
screen, and no test could have noticed because the whole layer was declared
untestable.

These read the source. Not a substitute for running it, but they pin the
invariants that were violated.
"""

import pathlib
import re

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
MAIN = ROOT / "firmware" / "src" / "main.cpp"


def source_without_comments(path):
    """Strip comments before matching.

    House rule, learned repeatedly: a comment explaining a removed defect
    contains the very text a search looks for, so a pin that reads the raw
    file can pass while the defect is back.
    """
    text = path.read_text()
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return text


@pytest.fixture
def main_src():
    return source_without_comments(MAIN)


def test_is_change_is_called_at_most_once_per_read(main_src):
    """isChange() updates its own last-seen state, so a second call in the
    same pass returns false. One call per keyboard read, no exceptions."""
    for func in ("readKey", "runSetup"):
        body = _function_body(main_src, func)
        assert body is not None, f"{func} not found"
        assert body.count("isChange()") <= 1, (
            f"{func} calls isChange() more than once; the second call "
            f"consumes the event and yields an empty key")


def test_the_main_loop_does_not_poll_the_keyboard_separately(main_src):
    """An extra poll anywhere in loop() re-introduces the same bug."""
    body = _function_body(main_src, "loop")
    assert body is not None
    assert "isChange()" not in body, (
        "loop() polls isChange() directly; it must go through readKey()")


def test_the_key_read_is_guarded_by_its_return_value(main_src):
    # `if (readKey(k))` — never a discarded call whose Key is used anyway.
    assert re.search(r"if\s*\(\s*readKey\s*\(\s*k\s*\)\s*\)", main_src), \
        "the key read is not guarded by its own result"


def test_mapping_lives_in_the_tested_core(main_src):
    """The character-to-direction mapping must not drift back into the shell,
    where nothing can check it."""
    for arrow in ("';'", "'.'", "','"):
        assert arrow not in main_src, (
            f"{arrow} is mapped in the shell again; it belongs in "
            f"lib/core/keymap.cpp where it is tested")


def test_the_shell_uses_the_pure_mapper(main_src):
    assert "core::mapKey" in main_src
    assert "core::hasKeyEvent" in main_src


def _function_body(src, name):
    """Crude brace matcher — enough for one C++ function body."""
    m = re.search(rf"\b{name}\s*\([^)]*\)\s*\{{", src)
    if not m:
        return None
    start = m.end() - 1
    depth = 0
    for i in range(start, len(src)):
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0:
                return src[start:i + 1]
    return None


def test_the_brace_matcher_actually_works():
    """A pin whose helper silently returns None would pass on anything."""
    src = "void f() { if (x) { y(); } }\nvoid g() { z(); }"
    assert _function_body(src, "f") == "{ if (x) { y(); } }"
    assert _function_body(src, "g") == "{ z(); }"
    assert _function_body(src, "missing") is None
