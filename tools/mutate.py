#!/usr/bin/env python3
"""Mutation probe: break a guarantee on purpose and require the suite to notice.

A test you have never seen fail is not an assurance. This runs each mutation,
reports whether the suite caught it, and restores the file in a `finally` —
an earlier ad-hoc version of this crashed mid-run and left three mutated files
in the tree, which is exactly the failure mode this script exists to avoid.

Usage:  python3 tools/mutate.py firmware   |   python3 tools/mutate.py gateway
"""

import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

SUITES = {
    "firmware": (ROOT / "firmware", ["pio", "test", "-e", "native"]),
    "gateway": (ROOT / "gateway", ["python3", "-m", "pytest", "tests/", "-q"]),
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
        try:
            path.write_text(original.replace(old, new, 1))
            caught = not run(cwd, cmd)
        finally:
            path.write_text(original)          # always, even on Ctrl-C
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
