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
HWNET = ROOT / "firmware" / "src" / "hw_net.cpp"
HWUI = ROOT / "firmware" / "src" / "hw_ui.cpp"


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


@pytest.fixture
def hwnet_src():
    return source_without_comments(HWNET)


@pytest.fixture
def hwui_src():
    return source_without_comments(HWUI)


def test_the_baked_seed_is_not_gated_on_an_empty_nvs(main_src):
    """The compiled-in credentials of a local build must apply once per
    distinct value set. The original gate — seed only when store::load()
    fails — kept a device on whatever the FIRST local build baked: load()
    accepts a config with an empty host, so a stale host survived every
    reflash and the device silently aimed at nothing."""
    body = _function_body(main_src, "setup")
    assert body is not None
    fp = body.find("configFingerprint")
    gate = body.find("if (!store::load(g_cfg))")
    assert fp != -1, "the seed no longer fingerprints the baked values"
    assert gate != -1, "the setup fallback gate is gone"
    assert fp < gate, (
        "the baked seed runs inside/after the load gate again — a stored "
        "config then shadows changed compiled-in values forever")
    assert "seedFingerprint() != fp" in body, (
        "the seed must fire exactly when the baked values CHANGED; any other "
        "comparison either re-seeds every boot or never re-seeds at all")


def test_network_dead_ends_are_reported_not_silent(hwnet_src):
    """runJob's early returns (Wi-Fi down, mDNS discovery failed) used to
    return without a word: no error, no counter. The diagnostics screen then
    showed "Abrufe 0" on a device that was polling constantly."""
    for msg in ("WLAN-Verbindung fehlgeschlagen", "Gateway nicht gefunden"):
        at = hwnet_src.find(msg)
        assert at != -1, f"dead-end message {msg!r} is gone"
        before = hwnet_src[max(0, at - 300):at]
        after = hwnet_src[at:at + 300]
        assert "++g_status.failed" in before, (
            f"the dead end behind {msg!r} no longer counts as a failure")
        assert "deliver(r)" in after, (
            f"the dead end behind {msg!r} no longer answers the UI task")


def test_the_diagnostics_screen_names_the_configured_gateway(hwui_src):
    """Before the first request there is no URL, no status and no error. The
    only way to see a stale or empty host from the screen is to print the
    configured target itself."""
    body = _function_body(hwui_src, "drawDiagnostics")
    assert body is not None
    assert '"GW %s:%u  Token %s"' in body, (
        "diagnostics no longer draws the configured host and port")
    assert "FEHLT" in body, "diagnostics no longer flags a missing token"


def test_the_wire_format_is_built_in_the_tested_core(main_src):
    """Inline JSON in the shell once sent a named value in the wrong field —
    every input/effect/mode press became a 400. The format now lives in
    core::buildActionBody, pinned from both ends by the contract test."""
    body = _function_body(main_src, "sendIntent")
    assert body is not None
    assert "core::buildActionBody" in body
    assert '{\\"target\\"' not in main_src, "hand-built JSON is back in the shell"


def test_an_unsent_action_is_rolled_back(main_src):
    """requestAction() fails when the job queue is full — a realistic case,
    presses pile up during a reconnect. Ignoring that left the optimistic
    change on screen with nothing ever sent."""
    body = _function_body(main_src, "sendIntent")
    assert "!net::requestAction(" in body, (
        "the result of requestAction is ignored again")
    after = body[body.index("!net::requestAction("):]
    assert "reject(token)" in after, "a dropped action no longer rolls back"


def test_a_restored_snapshot_is_flagged_not_zeroed(main_src):
    """Zeroing receivedAtMs made 'now minus receivedAt' read as uptime: fresh
    for 8 s, then 'Stand 12s alt' on data that was forty minutes old."""
    body = _function_body(main_src, "setup")
    assert "core::markRestoredFromSleep(g_dash)" in body
    assert "receivedAtMs = 0" not in main_src, (
        "the wake path zeroes the timestamp again, which is uptime, not age")


def test_snapshot_polls_are_coalesced(hwnet_src):
    """During an outage the poller kept queueing snapshot jobs until the
    queue was full of them; presses were then dropped, and the worker stayed
    busy on reconnect attempts so the device could never sleep."""
    body = _function_body(hwnet_src, "requestDash")
    assert body is not None
    assert "if (g_dashPending) return true;" in body, (
        "a second snapshot request is queued instead of folded")
    worker = _function_body(hwnet_src, "worker")
    assert "g_dashPending = false" in worker, (
        "the pending flag is never cleared; polling would stop for good")
    # Flag before send. The worker clears the flag when it takes the job; a
    # flag raised after the send can be raised after that clear, and then
    # nothing is queued behind it — polling stops for good.
    assert body.index("g_dashPending = true") < body.index("xQueueSend("), (
        "the pending flag is raised after the job is queued (race with the "
        "worker's clear)")
    assert "g_dashPending = false" in body, (
        "a failed queue insert leaves the flag raised")


def test_polls_back_off_while_wifi_is_down_but_presses_still_try(hwnet_src):
    connect = _function_body(hwnet_src, "connectWifi")
    assert "retryAtMs = millis() + core::backoffDelay(" in connect, (
        "a failed connect no longer schedules a backoff")
    job = _function_body(hwnet_src, "runJob")
    assert "if (job.isDash && wait > 0)" in job, (
        "snapshot polls no longer fail fast during backoff — the worker "
        "spends seconds per poll reconnecting and busy() never drops")


def test_action_verdicts_have_their_own_queue(hwnet_src):
    """A snapshot landing right after an action overwrote the action's
    refusal (length-1 overwrite queue); the optimistic change then stayed on
    screen with nobody to roll it back."""
    deliver = _function_body(hwnet_src, "deliver")
    assert deliver is not None, "deliver() is gone"
    assert "xQueueSend(g_verdicts" in deliver
    assert "xQueueOverwrite(g_replies" in deliver
    take = _function_body(hwnet_src, "takeResult")
    assert (take.index("xQueueReceive(g_verdicts") <
            take.index("xQueueReceive(g_replies")), (
        "verdicts must be drained before snapshots: they are what a press "
        "is waiting on")


def test_a_reused_lease_is_dropped_after_a_transport_failure(hwnet_src):
    """The fast wake path reuses the last DHCP lease. If the router handed
    that address to someone else meanwhile, WiFi.status() still says
    connected and every request fails — for up to 24 h, the hint's TTL."""
    job = _function_body(hwnet_src, "runJob")
    at = job.find("code <= 0 && g_status.usedFastPath")
    assert at != -1, "a transport failure no longer questions the reused lease"
    assert "clearApHint()" in job[at:at + 300]


def test_the_header_wording_comes_from_the_tested_core(hwui_src):
    header = _function_body(hwui_src, "drawHeader")
    assert "core::ageLabel(" in header
    assert "Stand %lus alt" not in hwui_src, (
        "the age wording is hand-rolled in the renderer again, where no test "
        "can see what it says after a wake")


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
