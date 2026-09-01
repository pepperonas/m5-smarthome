#!/usr/bin/env python3
"""Mutation probe: break a guarantee on purpose and require the suite to notice.

A test you have never seen fail is not an assurance. This runs each mutation,
reports whether the suite caught it, and restores the file in a `finally` —
an earlier ad-hoc version of this crashed mid-run and left three mutated files
in the tree, which is exactly the failure mode this script exists to avoid.

A `finally` does not survive SIGKILL, and that door was walked through too: a
killed run left the "rollback is a no-op" mutation in optimistic.cpp, and the
next suite run blamed a guarantee no commit had broken. So the original text
is journalled to disk *before* the source file is touched, and every start
recovers whatever a dead run left behind.

Usage:  python3 tools/mutate.py firmware | gateway | shell
"""

import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

#: Records the original text of the file currently being mutated. Written
#: before the source file is touched and removed after it is restored, so its
#: mere existence means a run died mid-mutation.
JOURNAL = ROOT / ".mutate-journal.json"


def begin_mutation(path, original):
    """Journal the original text, then let the caller mutate the file."""
    JOURNAL.write_text(json.dumps({str(path): original}))


def end_mutation(path):
    """Restore from the journal and drop it. Safe to call twice."""
    recover(quiet=True)


def recover(quiet=False):
    """Undo whatever a previous run left mutated. Returns the paths restored."""
    if not JOURNAL.exists():
        return []
    try:
        entries = json.loads(JOURNAL.read_text())
    except (json.JSONDecodeError, UnicodeDecodeError) as e:
        # Deleting it would destroy the only record of the original text.
        raise SystemExit(
            f"the mutation journal at {JOURNAL} is corrupt ({e}); a file may "
            f"still be mutated. Inspect it, restore by hand (git checkout), "
            f"then delete it.")
    restored = []
    for name, original in entries.items():
        path = pathlib.Path(name)
        if path.read_text() != original:
            if not quiet:
                print(f"  recovered {path.name} from an interrupted run")
        path.write_text(original)
        restored.append(path)
    JOURNAL.unlink()
    return restored


SUITES = {
    "firmware": (ROOT / "firmware", ["pio", "test", "-e", "native"]),
    "gateway": (ROOT / "gateway", ["python3", "-m", "pytest", "tests/", "-q"]),
    # The Arduino shell has no host tests; its invariants are pinned against
    # the source (tools/tests/test_firmware_shell.py). Those pins deserve the
    # same distrust as any other test.
    "shell": (ROOT, ["python3", "-m", "pytest",
                     "tools/tests/test_firmware_shell.py", "-q"]),
}

MUTATIONS = {
    "firmware": [
        ("rollback is a no-op", "lib/core/optimistic.cpp",
         "if (o.active && o.token == token) { o.active = false; return; }",
         "if (o.active && o.token == token) { return; }"),
        ("a broken reply wipes the last good state", "lib/core/dash.cpp",
         "    if (err) return false;",
         "    if (err) { out = Dash(); return false; }"),
        ("an IR press counts as confirmed", "lib/core/optimistic.cpp",
         "        if (o.viaIr) continue;", "        (void)0;"),
        ("overlays never expire", "lib/core/optimistic.cpp",
         "return o.active && (int32_t)(o.expiresAt - nowMs) > 0;",
         "return o.active;"),
        ("two typos are acted on", "lib/core/command.cpp",
         "        if (dist == 2) return 55;", "        if (dist == 2) return 65;"),
        ("the fingerprint may collide with the never-seeded sentinel",
         "lib/core/netplan.cpp", "    return h ? h : 1u;", "    return h;"),
        ("a stale AP hint is trusted", "lib/core/netplan.cpp",
         "    return (nowEpoch - h.savedAtEpoch) < kApHintTtlS;", "    return true;"),
        ("sleep interrupts an in-flight request", "lib/core/netplan.cpp",
         "    if (busy) return false;", "    if (false) return false;"),
        ("NEC command complement is dropped", "lib/core/ir_nec.cpp",
         "    const uint8_t cmdInv = static_cast<uint8_t>(~command);",
         "    const uint8_t cmdInv = command;"),
        ("a cycled action loses its name", "lib/core/ui_state.cpp",
         "                setStr(out.intent.name, sizeof(out.intent.name),\n"
         "                       kYamahaInputs[st.yamInput]);", "                (void)0;"),
        ("the remote paints over strip-warn", "lib/core/ui_state.cpp",
         "                if (d.lw.warnOwned) {", "                if (false) {"),
        ("Enter does nothing on the home screen", "lib/core/ui_state.cpp",
         "            if (k.enter || k.right) {\n"
         "                st.screen = homeScreenAt(st.cursor);",
         "            if (false) {\n"
         "                st.screen = homeScreenAt(st.cursor);"),
        ("the home cursor and the digit keys disagree", "lib/core/ui_state.cpp",
         "    return (idx >= 0 && idx < kHomeRowCount) ? kHomeRows[idx] : Screen::Home;",
         "    return (idx >= 0 && idx < kHomeRowCount) ? kHomeRows[(idx + 1) % kHomeRowCount] : Screen::Home;"),
    ],
    "shell": [
        ("changed baked credentials never reach a seeded device",
         "firmware/src/main.cpp",
         "if (baked.ssid[0] && baked.token[0] &&\n"
         "            store::seedFingerprint() != fp) {",
         "if (false) {"),
        ("a failed gateway discovery is silent again",
         "firmware/src/hw_net.cpp",
         '"Gateway nicht gefunden (mDNS)"',
         '""'),
        ("a network dead end no longer counts as a failure",
         "firmware/src/hw_net.cpp",
         "    if (g_cfg.host[0] == 0 && !discoverGateway()) {\n"
         "        ++g_status.requests;\n"
         "        ++g_status.failed;",
         "    if (g_cfg.host[0] == 0 && !discoverGateway()) {\n"
         "        ++g_status.requests;"),
        ("the diagnostics screen hides the configured target",
         "firmware/src/hw_ui.cpp",
         '"GW %s:%u  Token %s"',
         '"(noch kein Abruf)"'),
    ],
    "gateway": [
        ("the fog interlock is removed", "m5gw/actions.py",
         'if p.get("confirm") is not True:', "if False:"),
        ("toggle guesses instead of refusing", "m5gw/actions.py",
         'raise ActionError("state unknown, cannot toggle", status=409)',
         "return True"),
        ("stale sources are not marked", "m5gw/aggregate.py",
         "if fresh is not None and not any(i in fresh for i in inputs):",
         "if False:"),
        ("entertainment zones leak into the room list", "m5gw/aggregate.py",
         'if not isinstance(g, dict) or g.get("type") != _ROOM_TYPE:',
         "if not isinstance(g, dict):"),
        ("an empty token means no auth", "m5gw/app.py",
         "        if not want:\n            # A gateway that can ignite a fog "
         "machine does not run open.\n            return False",
         "        if not want:\n            return True"),
        ("the snapshot cache is bypassed", "m5gw/app.py",
         'if not force and cache["body"] and (now - cache["at"]) < config.CACHE_TTL:',
         "if False:"),
    ],
}


def run(cwd, cmd):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    return r.returncode == 0


def main():
    which = sys.argv[1] if len(sys.argv) > 1 else "firmware"
    if which not in SUITES:
        raise SystemExit(f"unknown suite {which!r}; pick one of {list(SUITES)}")
    cwd, cmd = SUITES[which]

    # A previous run may have been killed outright, leaving a mutated file
    # that would otherwise be blamed on the code under test.
    recover()

    if not run(cwd, cmd):
        raise SystemExit("baseline suite is red — fix that before mutating")

    survived = []
    for label, rel, old, new in MUTATIONS[which]:
        path = cwd / rel
        original = path.read_text()
        if old not in original:
            print(f"  SKIP  {label} (anchor text not found in {rel})")
            survived.append(label + " [anchor missing]")
            continue
        begin_mutation(path, original)     # on disk before the file changes
        try:
            path.write_text(original.replace(old, new, 1))
            caught = not run(cwd, cmd)
        finally:
            end_mutation(path)                 # always, even on Ctrl-C
        print(f"  {'caught' if caught else 'SURVIVED':>8}  {label}")
        if not caught:
            survived.append(label)

    print()
    if survived:
        print(f"{len(survived)} mutation(s) survived — those guarantees are "
              f"not actually tested:")
        for s in survived:
            print(f"  - {s}")
        return 1
    print(f"all {len(MUTATIONS[which])} mutations caught")
    return 0


if __name__ == "__main__":
    sys.exit(main())
