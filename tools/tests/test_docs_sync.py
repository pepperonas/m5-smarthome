"""Documentation checked against the code it describes.

Prose drifts silently: nothing breaks when a key is added and the table is
not, and the reader trusts the table. These tests make the drift loud.

Deliberately narrow. They assert facts that exist in both places — keys,
actions, ports, enumerations — and never try to review the writing.
"""

import pathlib
import re

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
README = ROOT / "README.md"
API_DOC = ROOT / "docs" / "API.md"
GATEWAY_DOC = ROOT / "docs" / "GATEWAY.md"
UI_SRC = ROOT / "firmware" / "lib" / "core" / "ui_state.cpp"
UI_HDR = ROOT / "firmware" / "lib" / "core" / "ui_state.h"
ACTIONS = ROOT / "gateway" / "m5gw" / "actions.py"
CONFIG = ROOT / "gateway" / "m5gw" / "config.py"


def strip_comments(text, style="c"):
    """House rule: match against code, not against comments.

    A comment explaining a removed defect contains the very words a search
    looks for, so a check that reads the raw file can pass while the defect
    is back. This has bitten this repository more than once.
    """
    if style == "c":
        text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
        text = re.sub(r"//[^\n]*", "", text)
    else:
        text = re.sub(r"#[^\n]*", "", text)
        text = re.sub(r'"""..*?"""', "", text, flags=re.S)
    return text


def documented_keys():
    """Single-character keys listed in the README key table."""
    keys = set()
    for line in README.read_text().splitlines():
        if not line.startswith("| `"):
            continue
        for token in re.findall(r"`([^`]+)`", line.split("|")[1]):
            if len(token) == 1:
                keys.add(token)
            elif token in ("Enter", "Tab", "Esc"):
                keys.add(token)
            else:
                for part in token.split():        # e.g. "`1`…`7`" ranges
                    if len(part) == 1:
                        keys.add(part)
    return keys


MAIN_SRC = ROOT / "firmware" / "src" / "main.cpp"


def implemented_keys():
    """Characters the firmware reacts to, from both places they are handled.

    Most live in the pure state machine; OTA is handled in the shell because
    core knows nothing about radios. Reading only one file reported a
    documented key as non-existent.
    """
    src = strip_comments(UI_SRC.read_text()) + strip_comments(MAIN_SRC.read_text())
    return set(re.findall(r"k\.ch == '(.)'", src))


#: Keys that are answers to a prompt rather than commands, so they belong in
#: the confirmation text on screen, not in a key table.
CONFIRMATION_KEYS = {"y", "j"}


def test_every_implemented_key_is_documented():
    missing = implemented_keys() - documented_keys() - CONFIRMATION_KEYS
    assert not missing, (
        f"keys handled in ui_state.cpp but absent from the README table: "
        f"{sorted(missing)}")


def test_the_key_table_documents_nothing_that_does_not_exist():
    # Digits are handled as a range, arrows via their own flags, so only
    # letters are compared here.
    letters = {k for k in documented_keys() if len(k) == 1 and k.isalpha()}
    stray = letters - implemented_keys()
    assert not stray, (
        f"README documents keys the firmware does not handle: {sorted(stray)}")


def test_every_screen_is_reachable_from_the_documented_keys():
    """A screen nobody can open is dead code wearing a feature's clothes."""
    hdr = strip_comments(UI_HDR.read_text())
    block = re.search(r"enum class Screen : uint8_t \{(.*?)\};", hdr, re.S)
    assert block, "Screen enum not found"
    screens = {s.strip() for s in block.group(1).replace("\n", "").split(",")
               if s.strip()}
    src = strip_comments(UI_SRC.read_text())
    for screen in screens:
        if screen == "Home":
            continue                      # the default, reached by Esc
        assert f"Screen::{screen}" in src, f"{screen} is never selected"


# --- the gateway contract -------------------------------------------------

def documented_actions():
    """(target, action) pairs from the action table in API.md."""
    pairs = set()
    for line in API_DOC.read_text().splitlines():
        m = re.match(r"\| `(\w+)` \| ([^|]+) \|", line)
        if not m:
            continue
        target = m.group(1)
        for action in re.findall(r"`(\w+)`", m.group(2)):
            pairs.add((target, action))
    return pairs


def implemented_actions():
    src = strip_comments(ACTIONS.read_text(), style="py")
    pairs = set()
    handlers = re.split(r"\ndef _(\w+)\(action", src)
    for i in range(1, len(handlers), 2):
        target = handlers[i]
        body = handlers[i + 1]
        for grp in re.findall(r'action in \(([^)]+)\)', body):
            for a in re.findall(r'"(\w+)"', grp):
                pairs.add((target, a))
        for a in re.findall(r'action == "(\w+)"', body):
            pairs.add((target, a))
    return pairs


#: Handler name in actions.py -> the target name used on the wire and in docs.
TARGET_ALIASES = {"lichtwerk": "lw", "yamaha": "yam", "teufel": "tf"}


def test_every_gateway_action_is_documented():
    impl = {(TARGET_ALIASES.get(t, t), a) for t, a in implemented_actions()}
    documented = documented_actions()
    assert impl, "no actions parsed; the parser is broken, not the docs"
    missing = impl - documented
    assert not missing, f"actions with no row in docs/API.md: {sorted(missing)}"


def test_the_documented_ports_match_the_gateway_config():
    cfg = CONFIG.read_text()
    ports = set(re.findall(r'"http://127\.0\.0\.1:(\d+)"', cfg))
    doc = GATEWAY_DOC.read_text()
    for port in ports:
        assert f"| {port} |" in doc, f"backend port {port} is undocumented"


def test_the_gateway_port_is_stated_consistently():
    port = re.search(r'M5GW_PORT", "(\d+)"', CONFIG.read_text()).group(1)
    for doc in (README, API_DOC, GATEWAY_DOC):
        text = doc.read_text()
        assert port in text, f"{doc.name} never mentions port {port}"
        # And no other five-digit gateway port has been left behind.
        stale = {p for p in re.findall(r":(\d{4})/api/", text)} - {port}
        assert not stale, f"{doc.name} refers to gateway port(s) {stale}"


# --- enumerations that exist in three places ------------------------------

def test_the_effect_list_matches_between_firmware_gateway_and_controller():
    """13 effects, minus iris_warn, in both halves — a name the gateway does
    not know is a 400 the user cannot explain."""
    fw = strip_comments((ROOT / "firmware" / "lib" / "core" / "ui_state.cpp").read_text())
    block = re.search(r"kLwEffects\[\] = \{(.*?)\};", fw, re.S)
    assert block, "effect list not found in firmware"
    fw_effects = set(re.findall(r'"(\w+)"', block.group(1)))

    gw = strip_comments(ACTIONS.read_text(), style="py")
    gw_block = re.search(r"_LW_EFFECTS = \{(.*?)\}", gw, re.S)
    gw_effects = set(re.findall(r'"(\w+)"', gw_block.group(1)))

    assert fw_effects == gw_effects, (
        f"firmware and gateway disagree: only firmware {fw_effects - gw_effects}, "
        f"only gateway {gw_effects - fw_effects}")
    assert "iris_warn" not in fw_effects


def test_the_disco_modes_match():
    fw = strip_comments((ROOT / "firmware" / "lib" / "core" / "ui_state.cpp").read_text())
    fw_modes = set(re.findall(r'"(\w+)"',
                   re.search(r"kDiscoModes\[\] = \{(.*?)\};", fw, re.S).group(1)))
    gw = strip_comments(ACTIONS.read_text(), style="py")
    gw_modes = set(re.findall(r'"(\w+)"',
                   re.search(r"_DISCO_MODES = \{(.*?)\}", gw, re.S).group(1)))
    assert fw_modes == gw_modes


def test_the_teufel_inputs_match():
    fw = strip_comments((ROOT / "firmware" / "lib" / "core" / "ui_state.cpp").read_text())
    fw_in = set(re.findall(r'"(\w+)"',
                re.search(r"kTeufelInputs\[\] = \{(.*?)\};", fw, re.S).group(1)))
    gw = strip_comments(ACTIONS.read_text(), style="py")
    gw_in = set(re.findall(r'"(\w+)"',
                re.search(r"_TF_INPUTS = \{(.*?)\}", gw, re.S).group(1)))
    assert fw_in == gw_in


# --- claims that would age badly ------------------------------------------

def test_no_stale_test_counts_in_prose():
    """Counts belong in the generated badges, which cannot go stale.

    Writing "69 tests" into a sentence guarantees it is wrong within a day —
    it already was, three times over, when this test was written.
    """
    for doc in (README, ROOT / "docs" / "ARCHITECTURE.md", GATEWAY_DOC):
        text = doc.read_text()
        # Allow it inside the badge block, which is regenerated.
        text = re.sub(r"<!-- badges:start -->.*?<!-- badges:end -->", "",
                      text, flags=re.S)
        hits = re.findall(r"\b\d+ (?:host )?tests\b", text)
        assert not hits, f"{doc.name} states a test count in prose: {hits}"


def test_the_measured_snapshot_size_is_stated_once_and_pinned():
    """721 bytes is quoted throughout as the argument for the gateway. It has
    to stay tied to the test that actually measures it."""
    budget = re.search(r"len\(body\.encode\(\)\) < (\d+)",
                       (ROOT / "gateway" / "tests" / "test_aggregate.py").read_text())
    assert budget, "the snapshot budget test disappeared"
    assert int(budget.group(1)) == 1024
    assert "721" in README.read_text()
    assert "721" in (ROOT / "docs" / "ARCHITECTURE.md").read_text()


def _slug(heading):
    """GitHub's heading anchor rule, near enough for this repository."""
    out = heading.strip().lower()
    out = re.sub(r"[^\w\s-]", "", out)
    return re.sub(r"\s+", "-", out)


def test_internal_anchors_point_at_real_headings():
    """A dead anchor in a table of contents is silent: the link works, it
    just lands nowhere useful."""
    for doc in sorted(ROOT.glob("*.md")) + sorted((ROOT / "docs").glob("*.md")):
        text = doc.read_text()
        headings = {_slug(m) for m in re.findall(r"^#{1,6}\s+(.+)$", text, re.M)}
        for anchor in re.findall(r"\]\(#([^)]+)\)", text):
            assert anchor in headings, f"{doc.name}: #{anchor} matches no heading"


def test_cross_document_links_resolve():
    """Links between documents, including their anchors."""
    for doc in sorted(ROOT.glob("*.md")) + sorted((ROOT / "docs").glob("*.md")):
        for target in re.findall(r"\]\((?!https?:|#)([^)]+)\)", doc.read_text()):
            path_part, _, anchor = target.partition("#")
            dest = (doc.parent / path_part).resolve()
            assert dest.exists(), f"{doc.name} links to missing {path_part}"
            if anchor and dest.suffix == ".md":
                headings = {_slug(m) for m in
                            re.findall(r"^#{1,6}\s+(.+)$", dest.read_text(), re.M)}
                assert anchor in headings, (
                    f"{doc.name} -> {path_part}#{anchor} matches no heading")


def test_every_doc_is_linked_from_the_readme():
    """A document nobody links to is a document nobody reads."""
    readme = README.read_text()
    for doc in sorted((ROOT / "docs").glob("*.md")):
        assert f"docs/{doc.name}" in readme, f"{doc.name} is never linked"


def test_documented_files_exist():
    """Every relative link in the README points at something real."""
    text = README.read_text()
    for target in re.findall(r"\]\((?!https?:)([^)#]+)", text):
        path = ROOT / target
        assert path.exists(), f"README links to missing {target}"
