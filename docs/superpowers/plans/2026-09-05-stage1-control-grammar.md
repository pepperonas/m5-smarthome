# Stage 1 — Control Grammar Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every app screen of the Cardputer remote becomes a list of controls driven by one key contract, with the seven existing screens migrated and behaving as before.

**Architecture:** A canonical `firmware/controls.json` describes every screen's controls (kind, label, bounds, accelerator); `tools/gen_controls.py` generates `lib/core/controls_table.h` and `docs/CONTROLS.md` from it, drift-checked like the IR table. A new pure module `lib/core/controls` builds the runtime list from the table plus the live snapshot, moves the cursor, and turns `adjust`/`activate` into the same `Intent`s the shell already sends. `ui_state::handleKey` delegates to it; `hw_ui` gets one `drawControls()` with a renderer per kind.

**Tech Stack:** C++17 (PlatformIO, Unity tests via `pio test -e native`), Python 3 (tools + pytest), M5GFX canvas.

**Spec:** `docs/superpowers/specs/2026-09-05-control-grammar-full-house-design.md` — this plan implements §2 (grammar), §3 for the functions that exist today, §6 (firmware structure) and the docs part of §7. No gateway change.

## Global Constraints

- Every decision lives in `lib/core` and is host-tested; the shell only draws and reads the keyboard.
- Wire bodies are unchanged in this stage: the contract block `ACTION_BODY_CONTRACT` in `test_main.cpp` must still pass, byte for byte.
- Key contract (spec §2): `;` `.` move, `,` `/` and `-` `+` adjust, `Enter` flips/opens/fires, `Space` flips the primary toggle, `` ` `` back, `Tab` console, digits jump from Home. Accelerators `m i e o p w` keep working.
- Home has 7 rows in this stage (row 8 `dB` is Stage 2) but already moves to **13-px rows**.
- Readouts are not selectable. Lists scroll around the cursor. A disabled control draws dim and refuses with a toast.
- Umlauts never reach the panel: all labels are ASCII (`Raeume`, `Hoehen`).
- Before every push: `./tools/preflight.sh` green, `python3 tools/mutate.py firmware|shell` all caught, no secret in the tree.
- Commits in English, one concern per commit.

---

## File map

| File | Responsibility |
|---|---|
| `firmware/controls.json` (create) | canonical control tables per screen |
| `tools/gen_controls.py` (create) | JSON → `controls_table.h` + `docs/CONTROLS.md` |
| `tools/tests/test_gen_controls.py` (create) | validates the JSON and the generator |
| `firmware/lib/core/controls_table.h` (generated, committed) | `Bind`, `Fmt`, `ControlSpec`, per-screen spec arrays, `specsFor()` |
| `firmware/lib/core/controls.h/.cpp` (create) | runtime `Control`/`ControlList`, `buildScreen`, cursor/scroll, `adjust`/`activate`, `parentScreen` |
| `firmware/test/test_core/test_controls.cpp` (create) | the module's tests |
| `firmware/lib/core/ui_state.h/.cpp` (modify) | `Screen::Room`, `UiState::roomId`, `handleKey` delegates to `controls` |
| `firmware/test/test_core/test_ui.cpp` (modify) | Enter opens a room, Space toggles; rest unchanged |
| `firmware/src/hw_ui.cpp` (modify) | `drawControls()` replaces `drawHome/drawRooms/drawDetail` |
| `tools/tests/test_firmware_shell.py` (modify) | renderer exhaustiveness pin |
| `tools/mutate.py` (modify) | new probes |
| `tools/preflight.sh`, `.github/workflows/build.yml` (modify) | controls drift check |
| `README.md`, `docs/ARCHITECTURE.md`, `docs/TESTING.md`, `CLAUDE.md` (modify) | key table, module rows, pitfalls |
| `tools/tests/test_docs_sync.py` (modify) | accelerator keys come from `controls.json` |

---

### Task 1: Canonical control table and generator

**Files:**
- Create: `firmware/controls.json`
- Create: `tools/gen_controls.py`
- Create: `tools/tests/test_gen_controls.py`
- Generate: `firmware/lib/core/controls_table.h`, `docs/CONTROLS.md`
- Modify: `tools/preflight.sh:32-35`, `.github/workflows/build.yml:70-78`

**Interfaces:**
- Produces: `controls_table.h` with

```cpp
namespace core {
enum class ControlKind : uint8_t { Toggle, Level, Choice, Picker, Stepper, Color, Action, Link, Readout };
enum class Fmt : uint8_t { Plain, Pct254, Pct255, Db10 };
enum class Bind : uint8_t { None, HomeRooms, HomeStrip, HomeYamaha, HomeTeufel, HomeDisco,
    HomeFog, HomeClimate, RoomOn, RoomBri, LwOn, LwBri, LwEffect, YamOn, YamVol, YamInput,
    YamMute, TfPath, TfOn, TfVol, TfInput, TfMute, DiscoOn, DiscoMode, FogOn, FogTank,
    FogNote, ClimaIn, ClimaOut, ClimaWx, ClimaWxDesc, ClimaPi };
struct ControlSpec {
    ControlKind kind; Bind bind; const char* label;
    int min, max, step; Fmt fmt; char key;      // key: accelerator or 0
};
const ControlSpec* specsFor(Screen s, int& count);   // nullptr for Console/Diagnostics
}
```

- [ ] **Step 1: Write the canonical table**

`firmware/controls.json`:

```json
{
  "_comment": "Canonical control tables. tools/gen_controls.py turns this into lib/core/controls_table.h and docs/CONTROLS.md. Labels are ASCII: the panel font has no umlauts.",
  "kinds": ["Toggle", "Level", "Choice", "Picker", "Stepper", "Color", "Action", "Link", "Readout"],
  "fmts": ["Plain", "Pct254", "Pct255", "Db10"],
  "screens": {
    "Home": [
      {"kind": "Link", "bind": "HomeRooms",   "label": "1 Raeume"},
      {"kind": "Link", "bind": "HomeStrip",   "label": "2 Strip"},
      {"kind": "Link", "bind": "HomeYamaha",  "label": "3 Yamaha"},
      {"kind": "Link", "bind": "HomeTeufel",  "label": "4 Teufel"},
      {"kind": "Link", "bind": "HomeDisco",   "label": "5 Disco"},
      {"kind": "Link", "bind": "HomeFog",     "label": "6 Nebel"},
      {"kind": "Link", "bind": "HomeClimate", "label": "7 Klima"}
    ],
    "Rooms": [],
    "Room": [
      {"kind": "Toggle", "bind": "RoomOn",  "label": "An", "key": "p"},
      {"kind": "Level",  "bind": "RoomBri", "label": "Helligkeit", "min": 1, "max": 254, "step": 30, "fmt": "Pct254"}
    ],
    "Lichtwerk": [
      {"kind": "Toggle", "bind": "LwOn",     "label": "Strom", "key": "p"},
      {"kind": "Level",  "bind": "LwBri",    "label": "Helligkeit", "min": 0, "max": 255, "step": 32, "fmt": "Pct255"},
      {"kind": "Choice", "bind": "LwEffect", "label": "Effekt", "key": "e"}
    ],
    "Yamaha": [
      {"kind": "Toggle", "bind": "YamOn",    "label": "Strom", "key": "p"},
      {"kind": "Level",  "bind": "YamVol",   "label": "Pegel", "min": -800, "max": -200, "step": 10, "fmt": "Db10"},
      {"kind": "Choice", "bind": "YamInput", "label": "Eingang", "key": "i"},
      {"kind": "Toggle", "bind": "YamMute",  "label": "Stumm", "key": "m"}
    ],
    "Teufel": [
      {"kind": "Toggle", "bind": "TfOn",    "label": "Strom ~", "key": "p"},
      {"kind": "Choice", "bind": "TfPath",  "label": "Weg", "key": "w"},
      {"kind": "Level",  "bind": "TfVol",   "label": "Lautst. ~", "min": 0, "max": 50, "step": 1, "fmt": "Plain"},
      {"kind": "Choice", "bind": "TfInput", "label": "Eingang ~", "key": "i"},
      {"kind": "Toggle", "bind": "TfMute",  "label": "Stumm ~", "key": "m"}
    ],
    "Disco": [
      {"kind": "Toggle", "bind": "DiscoOn",   "label": "Lichter", "key": "p"},
      {"kind": "Choice", "bind": "DiscoMode", "label": "Modus", "key": "o"}
    ],
    "Fog": [
      {"kind": "Toggle",  "bind": "FogOn",   "label": "Nebel", "key": "p"},
      {"kind": "Readout", "bind": "FogTank", "label": "Tank"},
      {"kind": "Readout", "bind": "FogNote", "label": "220 V und heiss."}
    ],
    "Climate": [
      {"kind": "Readout", "bind": "ClimaIn",     "label": "Innen"},
      {"kind": "Readout", "bind": "ClimaOut",    "label": "Garten"},
      {"kind": "Readout", "bind": "ClimaWx",     "label": "Wetter"},
      {"kind": "Readout", "bind": "ClimaWxDesc", "label": ""},
      {"kind": "Readout", "bind": "ClimaPi",     "label": "Pi"}
    ]
  }
}
```

- [ ] **Step 2: Write the failing generator tests**

`tools/tests/test_gen_controls.py`:

```python
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


def test_levels_carry_bounds_that_the_step_divides():
    """A step that does not divide the range leaves the top value unreachable
    from the bottom by stepping — the user can never hit 'max'."""
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
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `cd /Users/martin/claude/m5-smarthome && python3 -m pytest tools/tests/test_gen_controls.py -q`
Expected: ERROR — `ModuleNotFoundError: No module named 'gen_controls'`

- [ ] **Step 4: Write the generator**

`tools/gen_controls.py`:

```python
#!/usr/bin/env python3
"""Generate the control tables from firmware/controls.json.

Writes firmware/lib/core/controls_table.h and docs/CONTROLS.md. Both are
committed; `--check` exits non-zero when either differs from what the JSON
would produce — the same discipline as the IR table, for the same reason: a
hand-edited copy drifts silently.

Usage:  python3 tools/gen_controls.py          # write both
        python3 tools/gen_controls.py --check  # verify, write nothing
"""

import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
JSON = ROOT / "firmware" / "controls.json"
HEADER = ROOT / "firmware" / "lib" / "core" / "controls_table.h"
DOC = ROOT / "docs" / "CONTROLS.md"

# Screens that are not control lists and therefore have no table.
NON_LIST_SCREENS = ("Console", "Diagnostics")

KIND_WORD = {
    "Toggle": "an/aus", "Level": "Regler", "Choice": "Auswahl (sendet sofort)",
    "Picker": "Auswahl (Enter sendet)", "Stepper": "Schritt -/+", "Color": "Farbe",
    "Action": "Aktion", "Link": "oeffnet", "Readout": "Anzeige",
}


def load():
    return json.loads(JSON.read_text())


def _cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def render_header(table):
    kinds = table["kinds"]
    fmts = table["fmts"]
    binds = ["None"] + [c["bind"] for controls in table["screens"].values() for c in controls]
    out = []
    out.append("// GENERATED by tools/gen_controls.py from firmware/controls.json.")
    out.append("// Do not edit; edit the JSON and regenerate. Drift fails preflight.")
    out.append("#pragma once")
    out.append("")
    out.append("#include <cstdint>")
    out.append("")
    out.append('#include "ui_state.h"')
    out.append("")
    out.append("namespace core {")
    out.append("")
    out.append("enum class ControlKind : uint8_t { " + ", ".join(kinds) + " };")
    out.append("enum class Fmt : uint8_t { " + ", ".join(fmts) + " };")
    out.append("enum class Bind : uint8_t {")
    for i in range(0, len(binds), 6):
        out.append("    " + ", ".join(binds[i:i + 6]) + ",")
    out.append("};")
    out.append("")
    out.append("struct ControlSpec {")
    out.append("    ControlKind kind;")
    out.append("    Bind bind;")
    out.append("    const char* label;")
    out.append("    int min, max, step;")
    out.append("    Fmt fmt;")
    out.append("    char key;          // accelerator letter, 0 for none")
    out.append("};")
    out.append("")
    for screen, controls in table["screens"].items():
        out.append(f"constexpr int kSpecCount_{screen} = {len(controls)};")
        if not controls:
            out.append(f"constexpr const ControlSpec* kSpec_{screen} = nullptr;")
            out.append("")
            continue
        out.append(f"constexpr ControlSpec kSpec_{screen}[] = {{")
        for c in controls:
            key = f"'{c['key']}'" if c.get("key") else "0"
            out.append("    {ControlKind::%s, Bind::%s, %s, %d, %d, %d, Fmt::%s, %s}," % (
                c["kind"], c["bind"], _cstr(c["label"]),
                c.get("min", 0), c.get("max", 0), c.get("step", 1),
                c.get("fmt", "Plain"), key))
        out.append("};")
        out.append("")
    out.append("inline const ControlSpec* specsFor(Screen s, int& count) {")
    out.append("    switch (s) {")
    for screen in table["screens"]:
        out.append(f"        case Screen::{screen}: count = kSpecCount_{screen}; return kSpec_{screen};")
    out.append("        default: count = 0; return nullptr;")
    out.append("    }")
    out.append("}")
    out.append("")
    out.append("}  // namespace core")
    return "\n".join(out) + "\n"


def _range_text(c):
    if c["kind"] != "Level":
        return ""
    fmt = c.get("fmt", "Plain")
    lo, hi, st = c["min"], c["max"], c["step"]
    if fmt == "Db10":
        return f"{lo / 10:.1f} … {hi / 10:.1f} dB, Schritt {st / 10:g} dB"
    if fmt in ("Pct254", "Pct255"):
        return f"{lo} … {hi} (als %), Schritt {st}"
    return f"{lo} … {hi}, Schritt {st}"


def render_doc(table):
    out = ["# Controls", "",
           "*Generated from `firmware/controls.json` by `tools/gen_controls.py`; do not edit.*",
           "",
           "Every app screen is a list of controls. `;` `.` move the cursor, `,` `/` "
           "(or `-` `+`) adjust the selected control, `Enter` flips a toggle, opens a "
           "link or fires an action, `Space` flips the screen's primary toggle. "
           "Readouts are skipped by the cursor. See the design spec for the grammar.",
           ""]
    for screen, controls in table["screens"].items():
        out.append(f"## {screen}")
        out.append("")
        if not controls:
            out.append("*Built from the snapshot at runtime (one row per room).*")
            out.append("")
            continue
        out.append("| Control | Kind | Range | Key |")
        out.append("|---|---|---|---|")
        for c in controls:
            label = c["label"] or "—"
            out.append(f"| `{label}` | {KIND_WORD[c['kind']]} | {_range_text(c)} | "
                       f"{'`' + c['key'] + '`' if c.get('key') else ''} |")
        out.append("")
    return "\n".join(out).rstrip() + "\n"


def main(argv):
    table = load()
    header = render_header(table)
    doc = render_doc(table)
    if "--check" in argv:
        bad = []
        if not HEADER.exists() or HEADER.read_text() != header:
            bad.append(str(HEADER.relative_to(ROOT)))
        if not DOC.exists() or DOC.read_text() != doc:
            bad.append(str(DOC.relative_to(ROOT)))
        if bad:
            print("controls table drifted: " + ", ".join(bad) +
                  " — run python3 tools/gen_controls.py")
            return 1
        print("controls table is current")
        return 0
    HEADER.write_text(header)
    DOC.write_text(doc)
    n = sum(len(v) for v in table["screens"].values())
    print(f"{HEADER.relative_to(ROOT)}: {n} controls on {len(table['screens'])} screens")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
```

- [ ] **Step 5: Generate and run the tests**

Run: `python3 tools/gen_controls.py && python3 -m pytest tools/tests/test_gen_controls.py -q`
Expected: `firmware/lib/core/controls_table.h: 27 controls on 9 screens` then `8 passed`.

Note: `controls_table.h` includes `ui_state.h` for `Screen`; `Screen::Room` does not exist yet, so the firmware will not compile until Task 5 adds it. That is expected — the header is only included from `controls.h` (Task 2), which is not compiled before Task 5 wires it in.

- [ ] **Step 6: Add the drift check to preflight and CI**

In `tools/preflight.sh`, after the IR block:

```bash
echo "==> control tables match their source"
python3 tools/gen_controls.py --check
```

In `.github/workflows/build.yml`, after the IR step, a step with the same two commands (`python tools/gen_controls.py --check`).

- [ ] **Step 7: Commit**

```bash
git add firmware/controls.json tools/gen_controls.py tools/tests/test_gen_controls.py \
        firmware/lib/core/controls_table.h docs/CONTROLS.md tools/preflight.sh .github/workflows/build.yml
git commit -m "Generate the control tables from one canonical JSON"
```

---

### Task 2: `controls` module — types and `buildScreen`

**Files:**
- Create: `firmware/lib/core/controls.h`, `firmware/lib/core/controls.cpp`
- Create: `firmware/test/test_core/test_controls.cpp`
- Modify: `firmware/lib/core/ui_state.h` — add `Screen::Room` and `UiState::roomId`, `UiState::scroll` (needed to compile; `handleKey` is untouched until Task 5)
- Modify: `firmware/test/test_core/test_main.cpp` — forward-declare and `RUN_TEST` the new tests

**Interfaces:**
- Consumes: `ControlSpec`, `specsFor()` from Task 1; `Dash`, `UiState`, `Screen`, the `k*` choice lists from `ui_state.h`.
- Produces:

```cpp
namespace core {
constexpr int kMaxControls = 16;
constexpr int kControlTextLen = 20;
struct Control {
    ControlKind kind; Bind bind; const char* label;
    int value;                     // Level: current; Choice: index; Toggle: 0/1; Link: room id or screen
    char text[kControlTextLen];    // right-hand text for Choice/Readout/Link
    int min, max, step; Fmt fmt;
    int key;                       // room id on room rows, else 0
    bool enabled;                  // false: dim, refuses with a toast
    char accel;
    uint16_t swatch[8];            // Color only; unused this stage
};
struct ControlList { Control items[kMaxControls]; int count; int visibleRows; };
void buildScreen(Screen s, const Dash& d, const UiState& st, ControlList& out);
bool selectable(const Control& c);           // false for Readout
}
```

- [ ] **Step 1: Extend `ui_state.h` so the new module compiles**

In `enum class Screen`, add `Room` after `Rooms`:

```cpp
enum class Screen : uint8_t {
    Home, Rooms, Room, Lichtwerk, Yamaha, Teufel, Disco, Fog, Climate, Console,
    Diagnostics,
};
```

In `struct UiState`, after `int cursor = 0;`:

```cpp
    int scroll = 0;               // first visible row of the current list
    int roomId = 0;               // which room Screen::Room shows
```

- [ ] **Step 2: Write the failing tests**

`firmware/test/test_core/test_controls.cpp`:

```cpp
// The control lists: what each screen offers, read from the snapshot.
#include <unity.h>

#include <cstring>

#include "controls.h"
#include "dash.h"
#include "ui_state.h"

using namespace core;

extern const char* liveSnapshotJson();     // shared with test_main.cpp

static Dash makeDash() {
    Dash d;
    const char* j = liveSnapshotJson();
    parseDash(j, strlen(j), d, 1000);
    return d;
}

static const Control* find(const ControlList& l, Bind b) {
    for (int i = 0; i < l.count; ++i) if (l.items[i].bind == b) return &l.items[i];
    return nullptr;
}

void test_every_list_screen_builds_within_the_slot_limit(void) {
    Dash d = makeDash();
    UiState st;
    const Screen screens[] = {Screen::Home, Screen::Rooms, Screen::Room, Screen::Lichtwerk,
                              Screen::Yamaha, Screen::Teufel, Screen::Disco, Screen::Fog,
                              Screen::Climate};
    for (Screen s : screens) {
        ControlList l;
        st.roomId = 81;
        buildScreen(s, d, st, l);
        TEST_ASSERT_TRUE(l.count > 0);
        TEST_ASSERT_TRUE(l.count <= kMaxControls);
    }
}

void test_home_rows_carry_the_house_status(void) {
    Dash d = makeDash();
    UiState st;
    ControlList l;
    buildScreen(Screen::Home, d, st, l);
    TEST_ASSERT_EQUAL(7, l.count);
    TEST_ASSERT_EQUAL_STRING("5 an", find(l, Bind::HomeRooms)->text);
    TEST_ASSERT_EQUAL_STRING("-28.0 Spotify", find(l, Bind::HomeYamaha)->text);
    TEST_ASSERT_EQUAL_STRING("aus  Tank 48%", find(l, Bind::HomeFog)->text);
    TEST_ASSERT_EQUAL_STRING("22.7 / 16.3 C", find(l, Bind::HomeClimate)->text);
}

void test_the_room_list_is_one_link_per_room(void) {
    Dash d = makeDash();
    UiState st;
    ControlList l;
    buildScreen(Screen::Rooms, d, st, l);
    TEST_ASSERT_EQUAL(d.hue.count, l.count);
    TEST_ASSERT_TRUE(l.items[2].kind == ControlKind::Link);
    TEST_ASSERT_EQUAL_STRING("Kueche", l.items[2].label);
    TEST_ASSERT_EQUAL(83, l.items[2].key);
    TEST_ASSERT_EQUAL_STRING("100%", l.items[2].text);      // bri 254
    TEST_ASSERT_EQUAL_STRING("aus", l.items[5].text);       // Garten off
}

void test_the_room_screen_reads_its_room(void) {
    Dash d = makeDash();
    UiState st;
    st.roomId = 85;                                          // Flur, on, bri 24
    ControlList l;
    buildScreen(Screen::Room, d, st, l);
    TEST_ASSERT_EQUAL(1, find(l, Bind::RoomOn)->value);
    TEST_ASSERT_EQUAL(24, find(l, Bind::RoomBri)->value);
    TEST_ASSERT_EQUAL(85, find(l, Bind::RoomBri)->key);
}

void test_levels_and_choices_show_the_current_value(void) {
    Dash d = makeDash();
    UiState st;
    ControlList l;
    buildScreen(Screen::Yamaha, d, st, l);
    TEST_ASSERT_EQUAL(-280, find(l, Bind::YamVol)->value);
    TEST_ASSERT_EQUAL_STRING("Spotify", find(l, Bind::YamInput)->text);
    TEST_ASSERT_EQUAL(0, find(l, Bind::YamMute)->value);

    buildScreen(Screen::Teufel, d, st, l);
    TEST_ASSERT_EQUAL_STRING("Netz", find(l, Bind::TfPath)->text);
    st.teufelUseIr = true;
    buildScreen(Screen::Teufel, d, st, l);
    TEST_ASSERT_EQUAL_STRING("IR (blind)", find(l, Bind::TfPath)->text);
    TEST_ASSERT_EQUAL(29, find(l, Bind::TfVol)->value);
    TEST_ASSERT_EQUAL(1, find(l, Bind::TfMute)->value);
}

void test_strip_controls_are_disabled_while_warn_owns_the_strip(void) {
    Dash d = makeDash();                     // lw.warn == true in the fixture
    UiState st;
    ControlList l;
    buildScreen(Screen::Lichtwerk, d, st, l);
    TEST_ASSERT_FALSE(find(l, Bind::LwEffect)->enabled);
    TEST_ASSERT_FALSE(find(l, Bind::LwBri)->enabled);
    TEST_ASSERT_TRUE(find(l, Bind::LwOn)->enabled);       // power still yours
    d.lw.warnOwned = false;
    buildScreen(Screen::Lichtwerk, d, st, l);
    TEST_ASSERT_TRUE(find(l, Bind::LwEffect)->enabled);
}

void test_readouts_are_not_selectable(void) {
    Dash d = makeDash();
    UiState st;
    ControlList l;
    buildScreen(Screen::Fog, d, st, l);
    TEST_ASSERT_TRUE(selectable(l.items[0]));             // the toggle
    TEST_ASSERT_FALSE(selectable(*find(l, Bind::FogTank)));
    TEST_ASSERT_EQUAL_STRING("48% (120 ml)", find(l, Bind::FogTank)->text);
}

void test_accelerators_come_from_the_table(void) {
    Dash d = makeDash();
    UiState st;
    ControlList l;
    buildScreen(Screen::Yamaha, d, st, l);
    TEST_ASSERT_EQUAL('m', find(l, Bind::YamMute)->accel);
    TEST_ASSERT_EQUAL('i', find(l, Bind::YamInput)->accel);
    TEST_ASSERT_EQUAL(0, find(l, Bind::YamVol)->accel);
}
```

Add to `test_main.cpp` next to the other forward declarations:

```cpp
void test_every_list_screen_builds_within_the_slot_limit(void);
void test_home_rows_carry_the_house_status(void);
void test_the_room_list_is_one_link_per_room(void);
void test_the_room_screen_reads_its_room(void);
void test_levels_and_choices_show_the_current_value(void);
void test_strip_controls_are_disabled_while_warn_owns_the_strip(void);
void test_readouts_are_not_selectable(void);
void test_accelerators_come_from_the_table(void);
```

and in `main()`, before `RUN_TEST(test_polling_is_fast_while_in_use_and_slow_when_idle);`:

```cpp
    RUN_TEST(test_every_list_screen_builds_within_the_slot_limit);
    RUN_TEST(test_home_rows_carry_the_house_status);
    RUN_TEST(test_the_room_list_is_one_link_per_room);
    RUN_TEST(test_the_room_screen_reads_its_room);
    RUN_TEST(test_levels_and_choices_show_the_current_value);
    RUN_TEST(test_strip_controls_are_disabled_while_warn_owns_the_strip);
    RUN_TEST(test_readouts_are_not_selectable);
    RUN_TEST(test_accelerators_come_from_the_table);
```

- [ ] **Step 3: Run to verify failure**

Run: `cd firmware && pio test -e native 2>&1 | grep -E "error|succeeded" | head -3`
Expected: `fatal error: 'controls.h' file not found`

- [ ] **Step 4: Write the module**

`firmware/lib/core/controls.h`:

```cpp
// Control lists: every app screen as rows the same five keys operate.
//
// The static shape of a screen (which controls, bounds, accelerators) is
// generated into controls_table.h from controls.json. This module fills the
// rows with the live snapshot, moves the cursor, and turns a key on a row
// into the same Intent the shell already knows how to send. Pure.
#pragma once

#include <cstdint>

#include "controls_table.h"
#include "dash.h"
#include "ui_state.h"

namespace core {

constexpr int kMaxControls = 16;
constexpr int kControlTextLen = 20;

struct Control {
    ControlKind kind = ControlKind::Readout;
    Bind bind = Bind::None;
    const char* label = "";
    int value = 0;                    // Level: current; Choice: index; Toggle: 0/1
    char text[kControlTextLen] = {0}; // right-hand text (Choice/Readout/Link)
    int min = 0, max = 0, step = 1;
    Fmt fmt = Fmt::Plain;
    int key = 0;                      // room id on room rows
    bool enabled = true;
    char accel = 0;
    uint16_t swatch[8] = {0};         // Color only; filled by a later stage
};

struct ControlList {
    Control items[kMaxControls];
    int count = 0;
    int visibleRows = 7;
};

// ⚠️ Lifetime: `Control::label` is a borrowed pointer. For controls from the
// table it points into static storage; for a room row it points at
// `Room::name` inside the Dash that built the list. A ControlList is
// therefore a short-lived local, valid only as long as the Dash it was built
// from. Never store one across frames, and never build one from a temporary.

// Fill `out` for screen `s` from the snapshot and the UI state.
void buildScreen(Screen s, const Dash& d, const UiState& st, ControlList& out);

// Readouts are shown but never selected.
bool selectable(const Control& c);

}  // namespace core
```

`firmware/lib/core/controls.cpp`:

```cpp
#include "controls.h"

#include <cstdio>
#include <cstring>

namespace core {
namespace {

void setText(Control& c, const char* s) {
    snprintf(c.text, sizeof(c.text), "%s", s ? s : "");
}

int indexOf(const char* value, const char* const* list, int count) {
    for (int i = 0; i < count; ++i) {
        if (value && strcmp(value, list[i]) == 0) return i;
    }
    return -1;
}

Control fromSpec(const ControlSpec& s) {
    Control c;
    c.kind = s.kind;
    c.bind = s.bind;
    c.label = s.label;
    c.min = s.min;
    c.max = s.max;
    c.step = s.step;
    c.fmt = s.fmt;
    c.accel = s.key;
    return c;
}

// Values that come from the house. Anything not listed keeps its defaults.
void fill(Control& c, const Dash& d, const UiState& st) {
    char b[kControlTextLen];
    switch (c.bind) {
        case Bind::HomeRooms:
            snprintf(b, sizeof(b), "%d an", d.hue.litCount);
            setText(c, d.valid ? b : "-");
            break;
        case Bind::HomeStrip:
            setText(c, d.lw.warnOwned ? "Warn-Modus" : (d.lw.on ? "an" : "aus"));
            break;
        case Bind::HomeYamaha:
            if (d.sourceOk(SRC_YAM)) {
                snprintf(b, sizeof(b), "%.1f %s", (double)d.yam.db,
                         d.yam.on ? d.yam.input : "aus");
                setText(c, b);
            } else {
                setText(c, "?");
            }
            break;
        case Bind::HomeTeufel:
            snprintf(b, sizeof(b), "%d %s ~", d.tf.volume, d.tf.input);
            setText(c, d.sourceOk(SRC_TF) ? b : "?");
            break;
        case Bind::HomeDisco:
            if (d.disco.on) snprintf(b, sizeof(b), "%d bpm  %.0f dB", d.disco.bpm, (double)d.disco.spl);
            else snprintf(b, sizeof(b), "aus  %.0f dB", (double)d.disco.spl);
            setText(c, b);
            break;
        case Bind::HomeFog:
            if (d.fog.tankPct >= 0) snprintf(b, sizeof(b), "%s  Tank %d%%", d.fog.on ? "AN" : "aus", d.fog.tankPct);
            else snprintf(b, sizeof(b), "%s", d.fog.on ? "AN" : "aus");
            setText(c, b);
            break;
        case Bind::HomeClimate:
            if (d.indoor.valid && d.outdoor.valid)
                snprintf(b, sizeof(b), "%.1f / %.1f C", (double)d.indoor.temp, (double)d.outdoor.temp);
            else if (d.indoor.valid)
                snprintf(b, sizeof(b), "%.1f C", (double)d.indoor.temp);
            else
                snprintf(b, sizeof(b), "?");
            setText(c, b);
            break;

        case Bind::RoomOn:
        case Bind::RoomBri: {
            const Room* r = findRoom(d, st.roomId);
            c.key = st.roomId;
            if (!r) { c.enabled = false; break; }
            c.value = c.bind == Bind::RoomOn ? (r->on ? 1 : 0) : (int)r->bri;
            break;
        }

        case Bind::LwOn:     c.value = d.lw.on ? 1 : 0; break;
        case Bind::LwBri:    c.value = d.lw.bri; c.enabled = !d.lw.warnOwned; break;
        case Bind::LwEffect: {
            const int i = indexOf(d.lw.effect, kLwEffects, kLwEffectCount);
            c.value = i < 0 ? st.lwEffect : i;
            setText(c, i < 0 ? d.lw.effect : kLwEffects[i]);
            c.enabled = !d.lw.warnOwned;
            break;
        }

        case Bind::YamOn:   c.value = d.yam.on ? 1 : 0; break;
        case Bind::YamVol:  c.value = d.yam.raw; break;
        case Bind::YamInput: {
            const int i = indexOf(d.yam.input, kYamahaInputs, kYamahaInputCount);
            c.value = i < 0 ? st.yamInput : i;
            setText(c, d.yam.input);
            break;
        }
        case Bind::YamMute: c.value = d.yam.mute ? 1 : 0; break;

        case Bind::TfPath:
            c.value = st.teufelUseIr ? 1 : 0;
            setText(c, st.teufelUseIr ? "IR (blind)" : "Netz");
            break;
        case Bind::TfOn:    c.value = d.tf.on ? 1 : 0; break;
        case Bind::TfVol:   c.value = d.tf.volume; break;
        case Bind::TfInput: {
            const int i = indexOf(d.tf.input, kTeufelInputs, kTeufelInputCount);
            c.value = i < 0 ? st.tfInput : i;
            setText(c, d.tf.input);
            break;
        }
        case Bind::TfMute:  c.value = d.tf.mute ? 1 : 0; break;

        case Bind::DiscoOn: c.value = d.disco.on ? 1 : 0; break;
        case Bind::DiscoMode: {
            const int i = indexOf(d.disco.mode, kDiscoModes, kDiscoModeCount);
            c.value = i < 0 ? st.discoMode : i;
            setText(c, d.disco.mode);
            break;
        }

        case Bind::FogOn:   c.value = d.fog.on ? 1 : 0; break;
        case Bind::FogTank:
            if (d.fog.tankPct >= 0) snprintf(b, sizeof(b), "%d%% (%d ml)", d.fog.tankPct, d.fog.tankMl);
            else snprintf(b, sizeof(b), "?");
            setText(c, b);
            break;
        case Bind::FogNote: break;

        case Bind::ClimaIn:
            if (d.indoor.valid) snprintf(b, sizeof(b), "%.1f C  %d%%", (double)d.indoor.temp, d.indoor.humidity);
            else snprintf(b, sizeof(b), "?");
            setText(c, b);
            break;
        case Bind::ClimaOut:
            if (d.outdoor.valid) snprintf(b, sizeof(b), "%.1f C  %d%%", (double)d.outdoor.temp, d.outdoor.humidity);
            else snprintf(b, sizeof(b), "?");
            setText(c, b);
            break;
        case Bind::ClimaWx:
            if (d.wx.valid) snprintf(b, sizeof(b), "%.1f C  %d/%d", (double)d.wx.temp, d.wx.high, d.wx.low);
            else snprintf(b, sizeof(b), "?");
            setText(c, b);
            break;
        case Bind::ClimaWxDesc:
            setText(c, d.wx.valid ? d.wx.desc : "");
            break;
        case Bind::ClimaPi:
            if (d.pi.valid) snprintf(b, sizeof(b), "%.0f%% CPU  %.0f C", (double)d.pi.cpu, (double)d.pi.temp);
            else snprintf(b, sizeof(b), "?");
            setText(c, b);
            break;

        case Bind::None:
        default:
            break;
    }
}

}  // namespace

bool selectable(const Control& c) { return c.kind != ControlKind::Readout; }

void buildScreen(Screen s, const Dash& d, const UiState& st, ControlList& out) {
    out.count = 0;
    out.visibleRows = s == Screen::Home ? 8 : 7;

    if (s == Screen::Rooms) {
        // One Link per room, built from the snapshot; the right-hand text is
        // what the old room list showed.
        for (int i = 0; i < d.hue.count && out.count < kMaxControls; ++i) {
            const Room& r = d.hue.rooms[i];
            Control c;
            c.kind = ControlKind::Link;
            c.bind = Bind::None;
            c.label = r.name;
            c.key = r.id;
            c.value = r.on ? 1 : 0;
            if (r.on) snprintf(c.text, sizeof(c.text), "%d%%", (r.bri * 100) / 254);
            else snprintf(c.text, sizeof(c.text), "aus");
            out.items[out.count++] = c;
        }
        return;
    }

    int n = 0;
    const ControlSpec* specs = specsFor(s, n);
    for (int i = 0; i < n && out.count < kMaxControls; ++i) {
        Control c = fromSpec(specs[i]);
        fill(c, d, st);
        out.items[out.count++] = c;
    }
}

}  // namespace core
```

- [ ] **Step 5: Run the tests**

Run: `pio test -e native 2>&1 | grep -E "FAIL|succeeded|error:"`
Expected: `120 test cases: 120 succeeded` (112 + 8).

- [ ] **Step 6: Commit**

```bash
git add firmware/lib/core/controls.h firmware/lib/core/controls.cpp \
        firmware/lib/core/ui_state.h firmware/test/test_core/test_controls.cpp \
        firmware/test/test_core/test_main.cpp
git commit -m "Add the controls module: every screen as rows built from the snapshot"
```

---

### Task 3: Cursor, scroll and the primary toggle

**Files:**
- Modify: `firmware/lib/core/controls.h`, `firmware/lib/core/controls.cpp`
- Modify: `firmware/test/test_core/test_controls.cpp`, `test_main.cpp` (registrations)

**Interfaces:**
- Produces:

```cpp
int firstSelectable(const ControlList& l);                   // -1 if none
int nextSelectable(const ControlList& l, int from, int dir); // wraps; skips Readouts
int firstVisible(const ControlList& l, int cursor, int scroll); // new scroll value
int primaryToggle(const ControlList& l);                     // first Toggle, else -1
int findAccel(const ControlList& l, char ch);                // index or -1
```

- [ ] **Step 1: Write the failing tests** (append to `test_controls.cpp`, register in `test_main.cpp` after `test_accelerators_come_from_the_table`)

```cpp
void test_the_cursor_skips_readouts_and_wraps(void) {
    Dash d = makeDash();
    UiState st;
    ControlList l;
    buildScreen(Screen::Fog, d, st, l);          // Toggle, Readout, Readout
    TEST_ASSERT_EQUAL(0, firstSelectable(l));
    TEST_ASSERT_EQUAL(0, nextSelectable(l, 0, +1));   // nothing else selectable: stays
    TEST_ASSERT_EQUAL(0, nextSelectable(l, 0, -1));

    buildScreen(Screen::Yamaha, d, st, l);       // four selectables
    TEST_ASSERT_EQUAL(1, nextSelectable(l, 0, +1));
    TEST_ASSERT_EQUAL(3, nextSelectable(l, 0, -1));   // wraps to the last
    TEST_ASSERT_EQUAL(0, nextSelectable(l, 3, +1));
}

void test_a_screen_of_readouts_has_no_cursor(void) {
    Dash d = makeDash();
    UiState st;
    ControlList l;
    buildScreen(Screen::Climate, d, st, l);
    TEST_ASSERT_EQUAL(-1, firstSelectable(l));
}

void test_scrolling_keeps_the_cursor_visible(void) {
    ControlList l;
    l.count = 12;
    l.visibleRows = 7;
    for (int i = 0; i < l.count; ++i) l.items[i].kind = ControlKind::Toggle;
    TEST_ASSERT_EQUAL(0, firstVisible(l, 0, 0));
    TEST_ASSERT_EQUAL(0, firstVisible(l, 6, 0));      // still on screen
    TEST_ASSERT_EQUAL(1, firstVisible(l, 7, 0));      // one past the bottom
    TEST_ASSERT_EQUAL(5, firstVisible(l, 11, 1));     // last row
    TEST_ASSERT_EQUAL(2, firstVisible(l, 2, 5));      // cursor above the window
}

void test_the_primary_toggle_is_the_first_toggle(void) {
    // Every real screen happens to lead with its power switch, so a synthetic
    // list is what proves the search actually looks past row 0 rather than
    // returning the first selectable row.
    ControlList l;
    l.count = 3;
    l.items[0].kind = ControlKind::Choice;
    l.items[1].kind = ControlKind::Level;
    l.items[2].kind = ControlKind::Toggle;
    TEST_ASSERT_EQUAL(2, primaryToggle(l));

    Dash d = makeDash();
    UiState st;
    buildScreen(Screen::Teufel, d, st, l);       // the real one leads with power
    TEST_ASSERT_EQUAL(0, primaryToggle(l));
    TEST_ASSERT_TRUE(l.items[0].bind == Bind::TfOn);
    buildScreen(Screen::Climate, d, st, l);
    TEST_ASSERT_EQUAL(-1, primaryToggle(l));     // all readouts
}

void test_accelerators_find_their_control(void) {
    Dash d = makeDash();
    UiState st;
    ControlList l;
    buildScreen(Screen::Yamaha, d, st, l);
    TEST_ASSERT_EQUAL(3, findAccel(l, 'm'));
    TEST_ASSERT_EQUAL(-1, findAccel(l, 'z'));
}
```

- [ ] **Step 2: Run to verify failure**

Run: `pio test -e native 2>&1 | grep -E "error:" | head -2`
Expected: `use of undeclared identifier 'firstSelectable'`

- [ ] **Step 3: Implement** — declarations in `controls.h` after `selectable`:

```cpp
// Cursor movement. Readouts are skipped; movement wraps. `from` may be -1.
int firstSelectable(const ControlList& l);
int nextSelectable(const ControlList& l, int from, int dir);

// The scroll window: returns the first visible row so that `cursor` is on
// screen, moving `scroll` as little as possible.
int firstVisible(const ControlList& l, int cursor, int scroll);

// Space flips this one: the first Toggle on the screen, or -1.
int primaryToggle(const ControlList& l);

// Index of the control whose accelerator is `ch`, or -1.
int findAccel(const ControlList& l, char ch);
```

Definitions in `controls.cpp` before the closing namespace:

```cpp
int firstSelectable(const ControlList& l) {
    for (int i = 0; i < l.count; ++i) if (selectable(l.items[i])) return i;
    return -1;
}

int nextSelectable(const ControlList& l, int from, int dir) {
    if (l.count == 0) return -1;
    int i = from;
    for (int n = 0; n < l.count; ++n) {
        i = (i + dir + l.count) % l.count;
        if (i < 0) i = 0;
        if (selectable(l.items[i])) return i;
    }
    return from;                       // nothing else to land on
}

int firstVisible(const ControlList& l, int cursor, int scroll) {
    const int rows = l.visibleRows > 0 ? l.visibleRows : 1;
    if (scroll < 0) scroll = 0;
    if (cursor < scroll) scroll = cursor;
    if (cursor >= scroll + rows) scroll = cursor - rows + 1;
    const int maxScroll = l.count > rows ? l.count - rows : 0;
    if (scroll > maxScroll) scroll = maxScroll;
    return scroll < 0 ? 0 : scroll;
}

int primaryToggle(const ControlList& l) {
    for (int i = 0; i < l.count; ++i)
        if (l.items[i].kind == ControlKind::Toggle) return i;
    return -1;
}

int findAccel(const ControlList& l, char ch) {
    if (!ch) return -1;
    for (int i = 0; i < l.count; ++i) if (l.items[i].accel == ch) return i;
    return -1;
}
```

- [ ] **Step 4: Run tests** — Expected: `125 test cases: 125 succeeded`.

- [ ] **Step 5: Commit**

```bash
git add firmware/lib/core/controls.h firmware/lib/core/controls.cpp \
        firmware/test/test_core/test_controls.cpp firmware/test/test_core/test_main.cpp
git commit -m "Cursor, scroll window and primary toggle for control lists"
```

---

### Task 4: `adjust` and `activate` — a key on a row becomes an Intent

**Files:**
- Modify: `firmware/lib/core/controls.h`, `firmware/lib/core/controls.cpp`
- Modify: `firmware/test/test_core/test_controls.cpp`, `test_main.cpp` (registrations)

**Interfaces:**
- Consumes: `Intent`, `KeyResult`, `toast()` from `ui_state.h`/`command.h`; `findRoom()` from `dash.h`.
- Produces:

```cpp
// `,` `/` `-` `+` on row idx. dir is -1 or +1. Mutates st (cycle indices,
// Teufel path) and may set a toast. Returns the intent to send, if any.
KeyResult adjust(const ControlList& l, int idx, int dir, const Dash& d, UiState& st, uint32_t nowMs);
// Enter on row idx: flips a Toggle, opens a Link, fires an Action.
KeyResult activate(const ControlList& l, int idx, const Dash& d, UiState& st, uint32_t nowMs);
// Space on the room list: the highlighted room, on/off.
KeyResult toggleRoom(const Dash& d, int roomId, UiState& st, uint32_t nowMs);
// Where Esc goes from `s`.
Screen parentScreen(Screen s);
```

- [ ] **Step 1: Write the failing tests** (append to `test_controls.cpp`; register after `test_accelerators_find_their_control`)

```cpp
static ControlList listFor(Screen s, const Dash& d, UiState& st) {
    ControlList l;
    buildScreen(s, d, st, l);
    return l;
}

static int idx(const ControlList& l, Bind b) {
    for (int i = 0; i < l.count; ++i) if (l.items[i].bind == b) return i;
    return -1;
}

void test_a_level_steps_and_clamps(void) {
    Dash d = makeDash();
    UiState st;
    st.roomId = 83;                                          // Kueche, bri 254
    ControlList l = listFor(Screen::Room, d, st);
    KeyResult up = adjust(l, idx(l, Bind::RoomBri), +1, d, st, 1000);
    TEST_ASSERT_TRUE(up.intent.valid);
    TEST_ASSERT_EQUAL_STRING("bri", up.intent.action);
    TEST_ASSERT_EQUAL(83, up.intent.arg);
    TEST_ASSERT_EQUAL(254, up.intent.arg2);                  // clamped, not 284
    TEST_ASSERT_EQUAL_STRING("Kueche 100%", up.intent.label);

    st.roomId = 85;                                          // Flur, bri 24
    l = listFor(Screen::Room, d, st);
    KeyResult dn = adjust(l, idx(l, Bind::RoomBri), -1, d, st, 1000);
    TEST_ASSERT_EQUAL(1, dn.intent.arg2);                    // never 0: that is "off"
}

void test_the_room_list_adjusts_brightness_straight_from_the_row(void) {
    Dash d = makeDash();
    UiState st;
    ControlList l = listFor(Screen::Rooms, d, st);
    KeyResult r = adjust(l, 4, +1, d, st, 1000);            // Flur row
    TEST_ASSERT_EQUAL_STRING("hue", r.intent.target);
    TEST_ASSERT_EQUAL_STRING("bri", r.intent.action);
    TEST_ASSERT_EQUAL(85, r.intent.arg);
    TEST_ASSERT_EQUAL(54, r.intent.arg2);                    // 24 + 30
}

void test_receiver_volume_is_a_step_and_stops_at_the_ceiling(void) {
    Dash d = makeDash();                                     // raw -280
    UiState st;
    ControlList l = listFor(Screen::Yamaha, d, st);
    KeyResult r = adjust(l, idx(l, Bind::YamVol), +1, d, st, 1000);
    TEST_ASSERT_EQUAL_STRING("vol", r.intent.action);
    TEST_ASSERT_EQUAL(2, r.intent.arg);                      // +1 dB = 2 raw steps
    TEST_ASSERT_EQUAL_STRING("-27.0 dB", r.intent.label);

    d.yam.raw = -200;                                        // at the top
    l = listFor(Screen::Yamaha, d, st);
    r = adjust(l, idx(l, Bind::YamVol), +1, d, st, 1000);
    TEST_ASSERT_FALSE(r.intent.valid);                       // nothing past max
}

void test_a_choice_cycles_from_what_is_shown(void) {
    Dash d = makeDash();
    strcpy(d.yam.input, "HDMI2");
    UiState st;
    ControlList l = listFor(Screen::Yamaha, d, st);
    KeyResult r = adjust(l, idx(l, Bind::YamInput), +1, d, st, 1000);
    TEST_ASSERT_EQUAL_STRING("input", r.intent.action);
    TEST_ASSERT_EQUAL_STRING("HDMI3", r.intent.name);
    r = adjust(l, idx(l, Bind::YamInput), -1, d, st, 1000);
    TEST_ASSERT_EQUAL_STRING("HDMI1", r.intent.name);
}

void test_a_disabled_control_refuses_with_a_toast(void) {
    Dash d = makeDash();                                     // warnOwned
    UiState st;
    ControlList l = listFor(Screen::Lichtwerk, d, st);
    KeyResult r = adjust(l, idx(l, Bind::LwEffect), +1, d, st, 1000);
    TEST_ASSERT_FALSE(r.intent.valid);
    TEST_ASSERT_EQUAL_STRING("Strip-Warn aktiv", st.toast);
}

void test_the_teufel_path_is_local(void) {
    Dash d = makeDash();
    UiState st;
    ControlList l = listFor(Screen::Teufel, d, st);
    KeyResult r = adjust(l, idx(l, Bind::TfPath), +1, d, st, 1000);
    TEST_ASSERT_FALSE(r.intent.valid);
    TEST_ASSERT_TRUE(st.teufelUseIr);
    l = listFor(Screen::Teufel, d, st);
    KeyResult v = adjust(l, idx(l, Bind::TfVol), +1, d, st, 1000);
    TEST_ASSERT_TRUE(v.viaIr);                               // now blind
    TEST_ASSERT_EQUAL_STRING("tf", v.intent.target);
}

void test_toggles_flip_from_the_current_state(void) {
    Dash d = makeDash();                                     // yam on, tf mute on
    UiState st;
    ControlList l = listFor(Screen::Yamaha, d, st);
    KeyResult r = activate(l, idx(l, Bind::YamOn), d, st, 1000);
    TEST_ASSERT_EQUAL_STRING("off", r.intent.action);
    d.yam.on = false;
    l = listFor(Screen::Yamaha, d, st);
    r = activate(l, idx(l, Bind::YamOn), d, st, 1000);
    TEST_ASSERT_EQUAL_STRING("on", r.intent.action);

    l = listFor(Screen::Teufel, d, st);
    r = activate(l, idx(l, Bind::TfMute), d, st, 1000);
    TEST_ASSERT_EQUAL_STRING("mute", r.intent.action);
    TEST_ASSERT_EQUAL_STRING("Mute: bekannt wirkungslos", st.toast);
}

void test_enter_on_a_level_or_choice_does_nothing(void) {
    Dash d = makeDash();
    UiState st;
    ControlList l = listFor(Screen::Yamaha, d, st);
    TEST_ASSERT_FALSE(activate(l, idx(l, Bind::YamVol), d, st, 1000).intent.valid);
    TEST_ASSERT_FALSE(activate(l, idx(l, Bind::YamInput), d, st, 1000).intent.valid);
}

void test_links_navigate(void) {
    Dash d = makeDash();
    UiState st;
    ControlList l = listFor(Screen::Home, d, st);
    activate(l, idx(l, Bind::HomeYamaha), d, st, 1000);
    TEST_ASSERT_TRUE(st.screen == Screen::Yamaha);
    TEST_ASSERT_EQUAL(0, st.cursor);

    st.screen = Screen::Rooms;
    l = listFor(Screen::Rooms, d, st);
    activate(l, 2, d, st, 1000);                             // Kueche
    TEST_ASSERT_TRUE(st.screen == Screen::Room);
    TEST_ASSERT_EQUAL(83, st.roomId);
}

void test_fog_on_asks_and_fog_off_does_not(void) {
    Dash d = makeDash();                                     // fog off
    UiState st;
    ControlList l = listFor(Screen::Fog, d, st);
    KeyResult r = activate(l, idx(l, Bind::FogOn), d, st, 1000);
    TEST_ASSERT_FALSE(r.intent.valid);
    TEST_ASSERT_TRUE(st.confirming);
    TEST_ASSERT_EQUAL_STRING("on", st.pending.action);

    st = UiState();
    d.fog.on = true;
    l = listFor(Screen::Fog, d, st);
    r = activate(l, idx(l, Bind::FogOn), d, st, 1000);
    TEST_ASSERT_EQUAL_STRING("off", r.intent.action);
    TEST_ASSERT_FALSE(st.confirming);
}

void test_space_toggles_the_highlighted_room(void) {
    Dash d = makeDash();
    UiState st;
    KeyResult r = toggleRoom(d, 81, st, 1000);               // Wohnzimmer, on
    TEST_ASSERT_EQUAL_STRING("off", r.intent.action);
    TEST_ASSERT_EQUAL(81, r.intent.arg);
    TEST_ASSERT_EQUAL_STRING("Wohnzimmer aus", r.intent.label);
}

void test_escape_goes_up_one_level(void) {
    TEST_ASSERT_TRUE(parentScreen(Screen::Room) == Screen::Rooms);
    TEST_ASSERT_TRUE(parentScreen(Screen::Rooms) == Screen::Home);
    TEST_ASSERT_TRUE(parentScreen(Screen::Yamaha) == Screen::Home);
    TEST_ASSERT_TRUE(parentScreen(Screen::Home) == Screen::Home);
}
```

- [ ] **Step 2: Run to verify failure** — Expected: `use of undeclared identifier 'adjust'`.

- [ ] **Step 3: Implement** — declarations in `controls.h`:

```cpp
// `,` `/` `-` `+` on row idx (dir -1/+1). May mutate cycle indices, the
// Teufel path and the toast. Returns the intent to send, if any.
KeyResult adjust(const ControlList& l, int idx, int dir, const Dash& d,
                 UiState& st, uint32_t nowMs);

// Enter on row idx: flips a Toggle, opens a Link, fires an Action.
KeyResult activate(const ControlList& l, int idx, const Dash& d, UiState& st,
                   uint32_t nowMs);

// Space on the room list: the highlighted room, on or off.
KeyResult toggleRoom(const Dash& d, int roomId, UiState& st, uint32_t nowMs);

// Where Esc goes from `s`.
Screen parentScreen(Screen s);
```

In `controls.cpp`, inside the anonymous namespace add:

```cpp
void setStr(char* dst, int cap, const char* src) {
    int n = 0;
    while (src && src[n] && n < cap - 1) { dst[n] = src[n]; ++n; }
    dst[n] = 0;
}

Intent make(const char* target, const char* action, int arg = 0, bool hasArg = false,
            int arg2 = 0, bool hasArg2 = false) {
    Intent i;
    i.valid = true;
    setStr(i.target, sizeof(i.target), target);
    setStr(i.action, sizeof(i.action), action);
    i.arg = arg; i.hasArg = hasArg; i.arg2 = arg2; i.hasArg2 = hasArg2;
    return i;
}

int cycle(int i, int n, int dir) { return n <= 0 ? 0 : (i + dir + n) % n; }

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

KeyResult refused(const Control& c, UiState& st, uint32_t nowMs) {
    KeyResult out;
    out.redraw = true;
    const bool strip = c.bind == Bind::LwBri || c.bind == Bind::LwEffect || c.bind == Bind::LwOn;
    toast(st, strip ? "Strip-Warn aktiv" : "nicht verfuegbar", nowMs);
    return out;
}

KeyResult roomBrightness(const Dash& d, int roomId, int dir, int step) {
    KeyResult out;
    out.redraw = true;
    const Room* r = findRoom(d, roomId);
    if (!r) return out;
    const int bri = clampi((int)r->bri + dir * step, 1, 254);
    out.intent = make("hue", "bri", roomId, true, bri, true);
    snprintf(out.intent.label, sizeof(out.intent.label), "%s %d%%", r->name, (bri * 100) / 254);
    return out;
}
```

and after `findAccel`:

```cpp
KeyResult adjust(const ControlList& l, int idx, int dir, const Dash& d,
                 UiState& st, uint32_t nowMs) {
    KeyResult out;
    out.redraw = true;
    if (idx < 0 || idx >= l.count) return out;
    const Control& c = l.items[idx];
    if (!c.enabled) return refused(c, st, nowMs);
    out.viaIr = st.teufelUseIr && (c.bind == Bind::TfVol || c.bind == Bind::TfInput);

    switch (c.kind) {
        case ControlKind::Toggle:
            return activate(l, idx, d, st, nowMs);

        case ControlKind::Link:
            // Room rows: brightness straight from the list, the fast path.
            if (c.key) return roomBrightness(d, c.key, dir, 30);
            return out;

        case ControlKind::Level: {
            const int next = clampi(c.value + dir * c.step, c.min, c.max);
            switch (c.bind) {
                case Bind::RoomBri:
                    return roomBrightness(d, c.key, dir, c.step);
                case Bind::LwBri:
                    out.intent = make("lw", "bri", 0, false, next, true);
                    snprintf(out.intent.label, sizeof(out.intent.label), "Strip %d%%", (next * 100) / 255);
                    return out;
                case Bind::YamVol:
                    if (next == c.value) return out;           // at the edge: nothing past it
                    out.intent = make("yam", "vol", dir * 2, true);
                    snprintf(out.intent.label, sizeof(out.intent.label), "%.1f dB", next / 10.0);
                    return out;
                case Bind::TfVol:
                    if (next == c.value) return out;
                    out.intent = make("tf", "vol", dir, true);
                    setStr(out.intent.label, sizeof(out.intent.label), dir > 0 ? "Teufel lauter" : "Teufel leiser");
                    return out;
                default:
                    return out;
            }
        }

        case ControlKind::Choice: {
            switch (c.bind) {
                case Bind::TfPath:
                    st.teufelUseIr = !st.teufelUseIr;
                    toast(st, st.teufelUseIr ? "Weg: IR (blind)" : "Weg: Netz", nowMs);
                    return out;
                case Bind::LwEffect: {
                    st.lwEffect = cycle(c.value, kLwEffectCount, dir);
                    out.intent = make("lw", "effect");
                    setStr(out.intent.name, sizeof(out.intent.name), kLwEffects[st.lwEffect]);
                    snprintf(out.intent.label, sizeof(out.intent.label), "Effekt %s", kLwEffects[st.lwEffect]);
                    return out;
                }
                case Bind::YamInput: {
                    st.yamInput = cycle(c.value, kYamahaInputCount, dir);
                    out.intent = make("yam", "input");
                    setStr(out.intent.name, sizeof(out.intent.name), kYamahaInputs[st.yamInput]);
                    snprintf(out.intent.label, sizeof(out.intent.label), "Eingang %s", kYamahaInputs[st.yamInput]);
                    return out;
                }
                case Bind::TfInput: {
                    st.tfInput = cycle(c.value, kTeufelInputCount, dir);
                    out.intent = make("tf", "input");
                    setStr(out.intent.name, sizeof(out.intent.name), kTeufelInputs[st.tfInput]);
                    snprintf(out.intent.label, sizeof(out.intent.label), "Eingang %s", kTeufelInputs[st.tfInput]);
                    return out;
                }
                case Bind::DiscoMode: {
                    st.discoMode = cycle(c.value, kDiscoModeCount, dir);
                    out.intent = make("disco", "mode");
                    setStr(out.intent.name, sizeof(out.intent.name), kDiscoModes[st.discoMode]);
                    snprintf(out.intent.label, sizeof(out.intent.label), "Modus %s", kDiscoModes[st.discoMode]);
                    return out;
                }
                default:
                    return out;
            }
        }

        case ControlKind::Picker:
        case ControlKind::Stepper:
        case ControlKind::Color:
        case ControlKind::Action:
        case ControlKind::Readout:
            return out;                 // later stages give these meaning
    }
    return out;
}

KeyResult activate(const ControlList& l, int idx, const Dash& d, UiState& st,
                   uint32_t nowMs) {
    KeyResult out;
    out.redraw = true;
    if (idx < 0 || idx >= l.count) return out;
    const Control& c = l.items[idx];
    if (!c.enabled) return refused(c, st, nowMs);
    out.viaIr = st.teufelUseIr && (c.bind == Bind::TfOn || c.bind == Bind::TfMute);
    const bool on = c.value != 0;

    switch (c.kind) {
        case ControlKind::Toggle:
            switch (c.bind) {
                case Bind::RoomOn:
                    return toggleRoom(d, c.key, st, nowMs);
                case Bind::LwOn:
                    out.intent = make("lw", on ? "off" : "on");
                    setStr(out.intent.label, sizeof(out.intent.label), on ? "Strip aus" : "Strip an");
                    return out;
                case Bind::YamOn:
                    out.intent = make("yam", on ? "off" : "on");
                    setStr(out.intent.label, sizeof(out.intent.label), on ? "Yamaha aus" : "Yamaha an");
                    return out;
                case Bind::YamMute:
                    out.intent = make("yam", "mute");
                    setStr(out.intent.label, sizeof(out.intent.label), on ? "Ton an" : "Stumm");
                    return out;
                case Bind::TfOn:
                    out.intent = make("tf", "power");
                    setStr(out.intent.label, sizeof(out.intent.label), "Teufel Power");
                    return out;
                case Bind::TfMute:
                    out.intent = make("tf", "mute");
                    // Documented house quirk: this byte reaches the box and
                    // does nothing. Say so rather than let the user think it
                    // is broken.
                    toast(st, "Mute: bekannt wirkungslos", nowMs);
                    return out;
                case Bind::DiscoOn:
                    out.intent = make("disco", on ? "off" : "on");
                    setStr(out.intent.label, sizeof(out.intent.label), on ? "Disco aus" : "Disco an");
                    return out;
                case Bind::FogOn:
                    if (on) {
                        // Switching a heater off is never gated.
                        out.intent = make("fog", "off");
                        setStr(out.intent.label, sizeof(out.intent.label), "Nebel aus");
                        return out;
                    } else {
                        Intent i = make("fog", "on");
                        i.needsConfirm = true;
                        setStr(i.label, sizeof(i.label), "Nebel AN");
                        st.confirming = true;
                        st.pending = i;
                        return out;                  // nothing sent yet
                    }
                default:
                    return out;
            }

        case ControlKind::Link:
            switch (c.bind) {
                case Bind::HomeRooms:   st.screen = Screen::Rooms; break;
                case Bind::HomeStrip:   st.screen = Screen::Lichtwerk; break;
                case Bind::HomeYamaha:  st.screen = Screen::Yamaha; break;
                case Bind::HomeTeufel:  st.screen = Screen::Teufel; break;
                case Bind::HomeDisco:   st.screen = Screen::Disco; break;
                case Bind::HomeFog:     st.screen = Screen::Fog; break;
                case Bind::HomeClimate: st.screen = Screen::Climate; break;
                default:
                    if (c.key) { st.roomId = c.key; st.screen = Screen::Room; }
                    break;
            }
            st.cursor = 0;
            st.scroll = 0;
            return out;

        case ControlKind::Level:
        case ControlKind::Choice:
        case ControlKind::Picker:
        case ControlKind::Stepper:
        case ControlKind::Color:
        case ControlKind::Action:
        case ControlKind::Readout:
            return out;
    }
    return out;
}

KeyResult toggleRoom(const Dash& d, int roomId, UiState& st, uint32_t nowMs) {
    (void)st; (void)nowMs;
    KeyResult out;
    out.redraw = true;
    const Room* r = findRoom(d, roomId);
    if (!r) return out;
    out.intent = make("hue", r->on ? "off" : "on", roomId, true);
    snprintf(out.intent.label, sizeof(out.intent.label), "%s %s", r->name, r->on ? "aus" : "an");
    return out;
}

Screen parentScreen(Screen s) {
    return s == Screen::Room ? Screen::Rooms : Screen::Home;
}
```

`activate` is used by `adjust` before it is defined: add `KeyResult activate(...)` declaration order in the header (already declared there — the .cpp includes it, so the order in the .cpp does not matter).

- [ ] **Step 4: Run tests** — Expected: `137 test cases: 137 succeeded`.

- [ ] **Step 5: Commit**

```bash
git add firmware/lib/core/controls.h firmware/lib/core/controls.cpp \
        firmware/test/test_core/test_controls.cpp firmware/test/test_core/test_main.cpp
git commit -m "adjust/activate: a key on a control row becomes the intent the shell sends"
```

---

### Task 5: `handleKey` delegates to the control lists

**Files:**
- Modify: `firmware/lib/core/ui_state.cpp` — replace everything from `// --- list navigation` to the end of `handleKey` (lines ~205–405)
- Modify: `firmware/test/test_core/test_ui.cpp` — the room Enter/Space tests, the primary-toggle expectation
- Modify: `firmware/test/test_core/test_main.cpp` (registrations)
- Modify: `firmware/src/main.cpp` — `claimFor` unchanged; `Screen::Room` needs no shell code

**Interfaces:**
- Consumes: everything from Tasks 2–4.
- Produces: `handleKey` with the spec §2 contract; `rowCount()` is removed (the renderer uses the list).

- [ ] **Step 1: Update the tests that encode the old contract** in `test_ui.cpp`

Replace `test_enter_toggles_the_selected_room` with:

```cpp
void test_enter_opens_the_room_and_space_toggles_it(void) {
    // Enter is "go" everywhere; Space is "power" everywhere. The room list
    // used to toggle on Enter, which left no key for opening the room.
    Dash d = makeDash();
    UiState st;
    st.screen = Screen::Rooms;
    st.cursor = 0;                                       // Wohnzimmer, on
    KeyResult r = handleKey(st, chr(' '), d, 1000);
    TEST_ASSERT_TRUE(r.intent.valid);
    TEST_ASSERT_EQUAL_STRING("off", r.intent.action);
    TEST_ASSERT_EQUAL(81, r.intent.arg);
    TEST_ASSERT_TRUE(st.screen == Screen::Rooms);

    r = handleKey(st, enterKey(), d, 1000);
    TEST_ASSERT_FALSE(r.intent.valid);
    TEST_ASSERT_TRUE(st.screen == Screen::Room);
    TEST_ASSERT_EQUAL(81, st.roomId);
    handleKey(st, escKey(), d, 1000);
    TEST_ASSERT_TRUE(st.screen == Screen::Rooms);         // one level up, not Home
    TEST_ASSERT_EQUAL(0, st.cursor);                     // back on the same room
}

void test_space_is_power_on_every_app_screen(void) {
    Dash d = makeDash();
    d.fog.on = true;
    const Screen apps[] = {Screen::Lichtwerk, Screen::Yamaha, Screen::Teufel,
                           Screen::Disco, Screen::Fog};
    for (Screen s : apps) {
        UiState st;
        st.screen = s;
        st.cursor = 2;                                   // not on the toggle
        KeyResult r = handleKey(st, chr(' '), d, 1000);
        TEST_ASSERT_TRUE_MESSAGE(r.intent.valid, "Space did not send");
        const bool power = strcmp(r.intent.action, "on") == 0 ||
                           strcmp(r.intent.action, "off") == 0 ||
                           strcmp(r.intent.action, "power") == 0;
        TEST_ASSERT_TRUE_MESSAGE(power, "Space sent something other than power");
    }
}

void test_the_cursor_never_rests_on_a_readout(void) {
    Dash d = makeDash();
    UiState st;
    st.screen = Screen::Fog;                             // Toggle, Readout, Readout
    handleKey(st, downKey(), d, 1000);
    TEST_ASSERT_EQUAL(0, st.cursor);
    handleKey(st, upKey(), d, 1000);
    TEST_ASSERT_EQUAL(0, st.cursor);
}

void test_accelerators_move_the_cursor_and_act(void) {
    Dash d = makeDash();
    UiState st;
    st.screen = Screen::Yamaha;
    KeyResult r = handleKey(st, chr('m'), d, 1000);
    TEST_ASSERT_EQUAL_STRING("mute", r.intent.action);
    TEST_ASSERT_EQUAL(3, st.cursor);                     // sits on Stumm now
}
```

Rename the registration in `test_main.cpp` (`test_enter_toggles_the_selected_room` → `test_enter_opens_the_room_and_space_toggles_it`) and add the three new ones after it.

- [ ] **Step 2: Run to verify failure**

Run: `pio test -e native 2>&1 | grep -E "FAIL" | head -5`
Expected: at least `test_enter_opens_the_room_and_space_toggles_it` and `test_space_is_power_on_every_app_screen` FAIL (Enter still toggles, Space does nothing).

- [ ] **Step 3: Rewrite the tail of `handleKey`**

Add `#include "controls.h"` to `ui_state.cpp`. Replace from the line `// --- list navigation ----` through the end of `handleKey` (keep the confirmation gate, console, global keys and the Home-only block above it) with:

```cpp
    // --- control screens ---------------------------------------------------
    // Every remaining screen is a list of controls. One contract: ; . move,
    // , / - + adjust, Enter flips/opens, Space flips the primary toggle,
    // letters jump to their control and act.
    ControlList list;
    buildScreen(st.screen, d, st, list);
    if (st.cursor >= list.count) st.cursor = list.count > 0 ? list.count - 1 : 0;
    if (st.cursor >= 0 && st.cursor < list.count && !selectable(list.items[st.cursor])) {
        st.cursor = firstSelectable(list);      // -1 on a screen of readouts
    }

    if (k.up || k.down) {
        if (st.cursor >= 0) st.cursor = nextSelectable(list, st.cursor, k.up ? -1 : +1);
        st.scroll = firstVisible(list, st.cursor < 0 ? 0 : st.cursor, st.scroll);
        return out;
    }

    if (k.ch == ' ') {
        if (st.screen == Screen::Rooms) {
            if (st.cursor >= 0 && st.cursor < list.count)
                return toggleRoom(d, list.items[st.cursor].key, st, nowMs);
            return out;
        }
        const int i = primaryToggle(list);
        if (i >= 0) return activate(list, i, d, st, nowMs);
        return out;
    }

    const int acc = findAccel(list, k.ch);
    if (acc >= 0) {
        st.cursor = acc;
        st.scroll = firstVisible(list, acc, st.scroll);
        // A letter on a Choice steps it; on anything else it is Enter.
        if (list.items[acc].kind == ControlKind::Choice)
            return adjust(list, acc, +1, d, st, nowMs);
        return activate(list, acc, d, st, nowMs);
    }

    if (k.left || k.right || k.ch == '+' || k.ch == '-') {
        if (st.cursor < 0) return out;
        return adjust(list, st.cursor, (k.right || k.ch == '+') ? +1 : -1, d, st, nowMs);
    }

    if (k.enter) {
        if (st.cursor < 0) return out;
        return activate(list, st.cursor, d, st, nowMs);
    }

    out.redraw = false;
    return out;
}
```

Change the global Esc/Del block so it goes one level up and lands on the room just left:

```cpp
    if (k.esc || k.del) {
        if (st.screen != Screen::Home) {
            const Screen from = st.screen;
            st.screen = parentScreen(st.screen);
            st.cursor = 0;
            st.scroll = 0;
            if (from == Screen::Room) {
                // Land on the room we came from, not the top of the list.
                for (int i = 0; i < d.hue.count; ++i)
                    if (d.hue.rooms[i].id == st.roomId) st.cursor = i;
            }
        }
        return out;
    }
```

Two existing tests encode the behaviour this step changes, and both must be
updated in the same commit:

- `test_escape_always_goes_home` — still true for every screen it iterates
  (they all sit directly under Home), but the name is now a claim the code
  does not make. Rename it to `test_escape_goes_one_level_up` and add
  `Screen::Room` to the loop, expecting `Screen::Rooms`.
- Anything referencing `rowCount()` must build the list instead:
  `ControlList l; buildScreen(st.screen, d, st, l);` and assert on `l.count`.
  Grep first: `grep -rn "rowCount" firmware/`.

Delete `rowCount()` from `ui_state.h`/`.cpp` and the `kHomeRows`-based `case Screen::Home` / `case Screen::Rooms` / per-app cases (they are now in `controls`). Keep `homeScreenAt()` and `screenForDigit()` — the digit shortcuts and `test_arrows_and_digits_agree_about_every_home_row` still use them; `homeScreenAt` must agree with the Home links, which the existing test checks by pressing Enter on each row.

- [ ] **Step 4: Run all tests**

Run: `pio test -e native 2>&1 | grep -E "FAIL|succeeded"`
Expected: `141 test cases: 141 succeeded`. If `test_every_screen_reachable_from_home_responds_to_enter` fails on Teufel, the first Teufel control is not a Toggle — the table in Task 1 puts `Strom ~` first for exactly this reason; check `controls.json`.

- [ ] **Step 5: Compile the shell** (it still references `rowCount` and draws the old way — fix the compile error only)

Run: `pio run -e cardputer 2>&1 | grep -E "error|SUCCESS"`
If `rowCount` is referenced in `hw_ui.cpp`, replace the reference with the list built in Task 6; until then, stub: keep `rowCount` declared in `ui_state.h` as `inline int rowCount(const UiState&, const Dash&) { return 0; }` marked `// removed in Task 6`. Prefer doing Task 6 immediately after.

- [ ] **Step 6: Commit**

```bash
git add firmware/lib/core/ui_state.h firmware/lib/core/ui_state.cpp \
        firmware/test/test_core/test_ui.cpp firmware/test/test_core/test_main.cpp
git commit -m "One key contract for every app screen; Enter opens, Space is power"
```

---

### Task 6: One renderer for all controls

**Files:**
- Modify: `firmware/src/hw_ui.cpp` — remove `drawHome`, `drawRooms`, `drawDetail`; add `drawControls`
- Modify: `tools/tests/test_firmware_shell.py`, `tools/mutate.py`

**Interfaces:**
- Consumes: `ControlList`, `buildScreen`, `firstVisible`, `selectable` from `controls.h`.
- Produces: `void drawControls(const core::ControlList&, const core::UiState&, int rowH)` inside `hw_ui.cpp`'s anonymous namespace; `draw()` builds the list itself.

- [ ] **Step 1: Write the shell pin first** (append to `test_firmware_shell.py` before `_function_body`)

```python
def test_the_renderer_draws_every_control_kind(hwui_src):
    """The list renderer has one case per ControlKind. A kind without a case
    draws nothing — a row that is there, selectable, and invisible."""
    body = _function_body(hwui_src, "drawControls")
    assert body is not None, "drawControls() is gone"
    kinds = ("Toggle", "Level", "Choice", "Picker", "Stepper", "Color",
             "Action", "Link", "Readout")
    missing = [k for k in kinds if f"case core::ControlKind::{k}:" not in body]
    assert not missing, f"drawControls has no case for {missing}"
    assert "default:" not in body, (
        "a default: arm hides a missing kind; list every kind explicitly")


def test_the_old_bespoke_screens_are_gone(hwui_src):
    for fn in ("drawHome", "drawRooms", "drawDetail"):
        assert _function_body(hwui_src, fn) is None, (
            f"{fn} is back; every app screen is drawn by drawControls")
```

Run: `python3 -m pytest tools/tests/test_firmware_shell.py -q` — Expected: 2 FAIL.

- [ ] **Step 2: Write the renderer**

In `hw_ui.cpp`, add `#include "controls.h"`, delete `drawHome`, `drawRooms` and `drawDetail` entirely, and add before `drawDiagnostics`:

```cpp
constexpr int kHomeRowH = 13;      // eight rows fit: 8 × 13 = 104 ≤ 108
constexpr int kLabelX = 6;
constexpr int kLabelXSel = 12;
constexpr int kBarX = 118;
constexpr int kBarW = 66;
constexpr int kRightX = board::kScreenW - 5;

void levelText(const core::Control& c, char* out, size_t cap) {
    switch (c.fmt) {
        case core::Fmt::Pct254: snprintf(out, cap, "%d%%", (c.value * 100) / 254); break;
        case core::Fmt::Pct255: snprintf(out, cap, "%d%%", (c.value * 100) / 255); break;
        case core::Fmt::Db10:   snprintf(out, cap, "%.1f dB", c.value / 10.0); break;
        case core::Fmt::Plain:  snprintf(out, cap, "%d", c.value); break;
    }
}

void drawControls(const core::ControlList& l, const core::UiState& st, int rowH) {
    const int first = core::firstVisible(l, st.cursor < 0 ? 0 : st.cursor, st.scroll);
    const int rows = l.visibleRows;
    char buf[32];

    for (int r = 0; r < rows && first + r < l.count; ++r) {
        const core::Control& c = l.items[first + r];
        const int y = kContentY + r * rowH;
        const bool sel = (first + r) == st.cursor && core::selectable(c);
        const uint16_t bg = sel ? 0x2124 : kBg;

        if (sel) {
            g_canvas->fillRect(0, y - 1, board::kScreenW, rowH, bg);
            g_canvas->fillRect(0, y - 1, 2, rowH, kAccent);
            g_canvas->setTextColor(kAccent, bg);
            g_canvas->drawString(">", 4, y);
        }

        const bool lit = c.kind == core::ControlKind::Toggle ? c.value != 0
                       : (c.kind == core::ControlKind::Link && c.key) ? c.value != 0 : false;
        uint16_t col = !c.enabled ? kDim : (lit ? kOn : kFg);
        if (c.kind == core::ControlKind::Readout) col = kDim;
        g_canvas->setTextColor(col, bg);
        g_canvas->drawString(c.label, sel ? kLabelXSel : kLabelX, y);

        g_canvas->setTextColor(c.enabled ? (c.kind == core::ControlKind::Readout ? kDim : kFg) : kDim, bg);
        switch (c.kind) {
            case core::ControlKind::Toggle:
                g_canvas->setTextColor(c.value ? kOn : (c.enabled ? kFg : kDim), bg);
                g_canvas->drawRightString(c.value ? "[an]" : "[aus]", kRightX, y);
                break;
            case core::ControlKind::Level: {
                const int span = c.max - c.min;
                const int fill = span > 0 ? ((c.value - c.min) * kBarW) / span : 0;
                g_canvas->drawRect(kBarX, y + 1, kBarW, 6, kDim);
                g_canvas->fillRect(kBarX, y + 1, fill < 0 ? 0 : fill, 6, c.enabled ? kAccent : kDim);
                levelText(c, buf, sizeof(buf));
                g_canvas->drawRightString(buf, kRightX, y);
                break;
            }
            case core::ControlKind::Choice:
            case core::ControlKind::Picker:
                snprintf(buf, sizeof(buf), "< %s >", c.text);
                g_canvas->drawRightString(buf, kRightX, y);
                break;
            case core::ControlKind::Stepper:
                g_canvas->drawRightString("<   >", kRightX, y);
                break;
            case core::ControlKind::Color: {
                // Eight swatches, a frame on the selected one. Later stages
                // fill c.swatch; an all-zero row draws eight dark squares.
                const int sw = 10, gap = 3;
                int x = kRightX - 8 * (sw + gap);
                for (int i = 0; i < 8; ++i, x += sw + gap) {
                    g_canvas->fillRect(x, y - 1, sw, sw, c.swatch[i]);
                    if (i == c.value) g_canvas->drawRect(x - 1, y - 2, sw + 2, sw + 2, kFg);
                }
                break;
            }
            case core::ControlKind::Action:
                g_canvas->drawRightString(">", kRightX, y);
                break;
            case core::ControlKind::Link:
                if (c.text[0]) g_canvas->drawRightString(c.text, kRightX - 12, y);
                g_canvas->drawRightString(">", kRightX, y);
                break;
            case core::ControlKind::Readout:
                g_canvas->drawRightString(c.text, kRightX, y);
                break;
        }
    }

    // Scroll mark: a 2-px track on the right edge, only when there is more
    // than the screen shows. A list that ends off-screen with no hint reads
    // as complete.
    if (l.count > rows) {
        const int trackY = kContentY - 1, trackH = rows * rowH;
        const int thumbH = (trackH * rows) / l.count;
        const int thumbY = trackY + (trackH * first) / l.count;
        g_canvas->fillRect(board::kScreenW - 2, trackY, 2, trackH, 0x2124);
        g_canvas->fillRect(board::kScreenW - 2, thumbY, 2, thumbH < 4 ? 4 : thumbH, kDim);
    }
}
```

Replace the body of `draw()`'s `switch (st.screen)`:

```cpp
    const char* hint = ";. waehlen  Enter oeffnen  d Diagnose";
    switch (st.screen) {
        case core::Screen::Console: drawConsole(st); hint = nullptr; break;
        case core::Screen::Diagnostics:
            drawDiagnostics(d, nowMs, link);
            hint = "Esc zurueck";
            break;
        default: {
            core::ControlList list;
            core::buildScreen(st.screen, d, st, list);
            const int rowH = st.screen == core::Screen::Home ? kHomeRowH : kRowH;
            drawControls(list, st, rowH);
            if (st.screen == core::Screen::Rooms)
                hint = "Enter oeffnet  Space an/aus  ,/ Helligkeit";
            else if (st.screen != core::Screen::Home)
                hint = ";. waehlen  ,/ aendern  Space an/aus";
            break;
        }
    }
```

(`drawControls`'s own `switch` on `c.kind` has no `default:`; the `switch (st.screen)` in `draw()` keeps its `default:` — the pin reads `drawControls` only.)

- [ ] **Step 3: Compile and run the pins**

Run: `pio run -e cardputer 2>&1 | grep -E "error|RAM|Flash|SUCCESS"` — Expected: SUCCESS; RAM ≤ 18 %.
Run: `cd .. && python3 -m pytest tools/tests/test_firmware_shell.py -q` — Expected: all pass.

- [ ] **Step 4: Add mutation probes** to `tools/mutate.py`

In `"shell"`:

```python
        ("a control kind has no renderer",
         "firmware/src/hw_ui.cpp",
         "            case core::ControlKind::Stepper:\n                g_canvas->drawRightString(\"<   >\", kRightX, y);\n                break;\n",
         "            default:\n                break;\n"),
```

In `"firmware"`:

```python
        ("the cursor lands on readouts", "lib/core/controls.cpp",
         "bool selectable(const Control& c) { return c.kind != ControlKind::Readout; }",
         "bool selectable(const Control& c) { (void)c; return true; }"),
        ("the scroll window loses the cursor", "lib/core/controls.cpp",
         "    if (cursor >= scroll + rows) scroll = cursor - rows + 1;",
         "    (void)rows;"),
        ("Space flips the wrong control", "lib/core/controls.cpp",
         "        if (l.items[i].kind == ControlKind::Toggle) return i;",
         "        if (selectable(l.items[i])) return i;"),
        ("a level walks past its ceiling", "lib/core/controls.cpp",
         "                    if (next == c.value) return out;           // at the edge: nothing past it",
         "                    (void)0;"),
        ("Enter on the room list toggles instead of opening", "lib/core/controls.cpp",
         "                    if (c.key) { st.roomId = c.key; st.screen = Screen::Room; }",
         "                    if (c.key) { st.roomId = c.key; }"),
```

Run: `python3 tools/mutate.py firmware && python3 tools/mutate.py shell` — Expected: all caught.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/hw_ui.cpp tools/tests/test_firmware_shell.py tools/mutate.py
git commit -m "One renderer for every control kind; the bespoke screens are gone"
```

---

### Task 7: Documentation, drift guards, flash

**Files:**
- Modify: `README.md` (Keys table, link to `docs/CONTROLS.md`), `docs/ARCHITECTURE.md` and `docs/TESTING.md` (module rows), `docs/PITFALLS.md`, `CLAUDE.md`
- Modify: `tools/tests/test_docs_sync.py`

- [ ] **Step 1: Teach the key pin about the table**

In `test_docs_sync.py`, extend `implemented_keys()`:

```python
def implemented_keys():
    """Characters the firmware reacts to: the state machine, the shell (OTA
    lives there), and the accelerators declared in controls.json."""
    src = strip_comments(UI_SRC.read_text()) + strip_comments(MAIN_SRC.read_text())
    keys = set(re.findall(r"k\.ch == '(.)'", src))
    table = json.loads((ROOT / "firmware" / "controls.json").read_text())
    for controls in table["screens"].values():
        for c in controls:
            if c.get("key"):
                keys.add(c["key"])
    return keys
```

(add `import json` at the top). Run `python3 -m pytest tools/tests/test_docs_sync.py -q` — expected: `test_every_implemented_key_is_documented` FAILS until Step 2 adds Space (`' '` is not a letter — document it as `Space`; add `" "` to the documented set by treating the literal word `Space` in the table as the space character).

Also in `documented_keys()`, map the word `Space` to `" "`:

```python
        if part == "Space":
            keys.add(" ")
```

- [ ] **Step 2: Rewrite the README Keys table**

Replace the table under `## Keys` with:

```markdown
| Key | Home | Inside an app |
|---|---|---|
| `;` `.` | move the cursor | move the cursor (readouts are skipped) |
| `Enter` | open the selected app | flip a toggle, open a room or a sub-list, fire an action |
| `Space` | — | the screen's power switch (first toggle); on the room list: the highlighted room |
| `,` `/` `-` `+` | `/` opens the command line | adjust the selected control: level ± one step, choice ± one entry; on the room list: brightness |
| `1`…`7` | jump straight to Rooms, Strip, Yamaha, Teufel, Disco, Fog, Climate | — |
| `Tab` | open the command line | complete a name (in the console) |
| `` ` `` / `Esc` | — | one level up (room → rooms → home) |
| `g` | Gute Nacht (asks first) | — |
| `a` | everything off | — |
| `d` | diagnostics (link, last HTTP status, snapshot age, heap) | — |
| `u` | over-the-air update mode | — |
| `w` | — | Teufel: switch between network and infrared |
| `m` | — | Yamaha / Teufel: mute |
| `p` | — | power (same as Space) |
| `i` | — | Yamaha / Teufel: step through inputs |
| `e` | — | Strip: step through the 13 effects |
| `o` | — | Disco: step through the 6 modes |

Every app screen is a list of controls with the same grammar; the full
per-screen table is generated from the source in
[docs/CONTROLS.md](docs/CONTROLS.md). A letter jumps the cursor to its
control and acts, so you always see what changed.
```

- [ ] **Step 3: Module rows and pitfalls**

`docs/ARCHITECTURE.md`, after the `| \`optimistic\` |` row: `| \`controls\` | every app screen as rows: what each row shows, what a key on it sends |`
`docs/TESTING.md`, same row.

`docs/PITFALLS.md`, before `### A reused DHCP lease can be someone else's now`:

```markdown
### Forty functions do not fit into forty letters

The first seven screens were hand-drawn and hand-keyed: `e`, `o`, `i`, `m`,
`w`, `p`. The spec adds about forty-five more functions. A letter each is a
cheat sheet nobody carries; a screen each is pixel code in the one layer
that cannot be tested. Every app screen is now a list of controls with one
key contract (`docs/CONTROLS.md`), generated from `firmware/controls.json`
so the table on the device, the renderer and the documentation cannot
disagree. Enter is "go", Space is "power", everywhere.
```

`CLAUDE.md`, in the house-rules section: `- **Every app screen is a control list** (\`lib/core/controls\`, table in \`firmware/controls.json\`, generated header + doc drift-checked). New functions are rows in the JSON, not letters.`

- [ ] **Step 4: Full preflight, both builds**

Run: `./tools/preflight.sh` — Expected: `preflight passed`.
Run: `cd firmware && pio run -e cardputer-local` — Expected: SUCCESS.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "Document the control grammar; the key table knows the accelerators from the source"
git push origin main
```

- [ ] **Step 6: Flash and hand the owner the checklist**

Flash `firmware/.pio/build/cardputer-local/m5-smarthome.bin` with the launcher. Ask the owner to confirm five things, in this order:

1. Home shows seven rows in the new look; `;` `.` move, `Enter` opens.
2. On **Yamaha**, `,` `/` on `Pegel` moves the bar and the receiver, `Space` toggles power, `m` lands on `Stumm` and mutes.
3. On **Raeume**, `Space` toggles the highlighted room, `Enter` opens it, `Esc` lands back on the same room.
4. On **Strip** with strip-warn active, `Effekt` is dim and `,` says `Strip-Warn aktiv`.
5. On **Nebel**, `Enter` asks first; `Klima` shows readouts with no cursor.

Anything that fails goes back to the task that owns it, with the screen's `d` diagnostics as evidence.

---

## Self-review

**Spec coverage (Stage 1 slice):** §2 grammar — Tasks 4/5 (keys), Task 3 (readouts skipped, scroll), Task 6 (scroll mark, dim + toast on disabled, Space, accelerators act visibly). §3 Home 13-px rows — Task 6 (`kHomeRowH`), seven rows this stage. §6 `controls` module — Tasks 2–4; renderer per kind — Task 6. §7 docs generated and drift-checked — Task 1 + Task 7; shell exhaustiveness pin — Task 6; device checklist — Task 7. Not in this stage by design: gateway changes, dB, colours, extras, and the `Color` swatch fill.

**Type consistency:** `KeyResult adjust(const ControlList&, int, int, const Dash&, UiState&, uint32_t)` and `activate(const ControlList&, int, const Dash&, UiState&, uint32_t)` are used identically in Tasks 4, 5, 6. `firstVisible(const ControlList&, int cursor, int scroll)` in Tasks 3, 5, 6. `Bind` names match `controls.json` (Task 1) everywhere.

**Order constraint, found by checking the plan against the existing suite:**
every app screen must lead with a Toggle, because
`test_every_screen_reachable_from_home_responds_to_enter` presses Enter with
the cursor at row 0 and requires an act. The first draft of the Teufel table
put `Weg` (a Choice) first, which would have made that test red — `Strom ~`
now leads, and `Weg` sits second. Consequently `primaryToggle` returns 0 on
every real screen, so Task 3 proves the search on a synthetic list whose
toggle is row 2.
