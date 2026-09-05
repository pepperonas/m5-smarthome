# Testing

The shape of it: **everything deterministic is pure and tested on the host;
everything that touches hardware is a thin adapter whose invariants are pinned
against its source.** That second half exists because the first half was not
enough — twice, the worst bug in the project sat in code declared too thin to
test.

Counts are in the README badges, which are generated from the suites
themselves. They are deliberately absent from this page.

## Running everything

```bash
./tools/preflight.sh
```

The order is load-bearing and that is why it is a script: adding a test changes
the test-count badge, so badges are regenerated **before** the drift check.
Doing the steps by hand in the wrong order looks like a spurious failure, and
the temptation is to push anyway. Which happened once.

Individually:

```bash
cd firmware && pio test -e native     # the pure core, no hardware
cd gateway  && python3 -m pytest -q   # aggregation, actions, XML, I/O
python3 -m pytest tools/tests         # tooling, image checks, doc drift
python3 tools/mutate.py firmware
python3 tools/mutate.py gateway
```

`pytest.ini` lets all Python suites run from the repository root. Without its
`pythonpath` entry, collecting `gateway/tests` from here fails to import
`m5gw` — and a collection error looks a lot like "no tests", which is how a
suite quietly stops being run.

## The four kinds of test here

### 1. Pure unit tests

`firmware/lib/core` and the pure halves of the gateway take data and return
data: no sockets, no display, not even a clock — callers pass `now`. That is
what makes the interesting half runnable on a laptop.

| Module | What it decides |
|---|---|
| `dash` | the model of the house, and how a snapshot parses into it |
| `keymap` | a keyboard report to a key press |
| `command` | fuzzy matching for the typed command line |
| `optimistic` | which local assumptions override the snapshot, for how long |
| `controls` | every app screen as rows: what each row shows, what a key on it sends |
| `netplan` | poll intervals, backlight steps, sleep threshold, backoff |
| `ui_state` | screens, key handling, the confirmation gate |
| `ir_teufel` | the generated code table, checked against its CSV |
| `reset_gesture` | when a held key means "wipe the credentials" |
| `ir_nec` | NEC bit framing — the arithmetic, not the timing |
| `aggregate` | eleven backend replies into one small snapshot |
| `actions` | one named action into one backend request |
| `yamaha_xml` | the receiver's XML into five fields |

### 2. Contract pins on untestable code

The Arduino shell needs real hardware, so `tools/tests/test_firmware_shell.py`
reads its source instead and asserts the invariants that were violated: at
most one `isChange()` per keyboard read, no direct keyboard polling in
`loop()`, the read guarded by its own return value, no arrow mapping outside
the tested core.

This is a weaker guarantee than running the code, and it is stated as such.
It is much stronger than nothing, which is what that layer had.

**Such checks must run against comment-free source.** A comment explaining a
removed defect contains the very words the check looks for, so a pin reading
the raw file passes while the defect is back. Every one of them strips
comments first.

### 3. Documentation drift

`tools/tests/test_docs_sync.py` compares prose against code:

- every key the firmware handles appears in the README table, and vice versa
- every gateway action has a row in `docs/API.md`
- every backend port is documented
- effect lists, disco modes and Teufel inputs agree between firmware and gateway
- no test count is written into a sentence, where it would be wrong within a day
- every relative link in the README points at something that exists

Prose drifts silently. Nothing breaks when a key is added and the table is
not, and the reader trusts the table.

### 4. Mutation probes

```bash
python3 tools/mutate.py firmware
python3 tools/mutate.py gateway
```

Each entry breaks one load-bearing guarantee on purpose and requires the suite
to go red — remove the fog interlock, let a broken reply wipe the last good
state, make an IR press count as confirmed, let two typos switch a room, make
an empty token mean "no auth".

The harness restores every file in a `finally`. An earlier ad-hoc version
crashed mid-run and left three mutated files in the tree, which is exactly the
failure it now guards against.

**Every new pin should be mutated once before it is trusted.** Several here
were green on arrival and proved nothing.

## Writing a test in this project

**A new test that fails is a suspected defect.** Investigate it; do not adjust
the expectation to match the observed behaviour. Several real defects in this
repository were found exactly that way — the dead fuzzy-match branch, the
index-dependent line count, the clock-dependent poller test.

Sometimes the investigation shows the *test* was wrong. Then fix it so it
asserts the intent rather than rubber-stamping what the code happens to do,
and record why:

- an alignment test asserted a file one byte under the slot would not fit. It
  does — the real slot is a multiple of the alignment, so there the two checks
  agree. It now pins a case where alignment actually decides, and says why the
  real one hides it.
- a "no build output" check searched the badge block for `RAM` and matched the
  `PSRAM required` badge. It now renders the block twice, with the build
  directory present and moved aside, and requires them to be identical.

**Assert on the mechanism, not on a word.** Word searches match comments,
adjacent features and their own source.

**Watch out for tests that cannot fail.** A helper that silently returns
`None`, an anchor that matches in both the fixed and broken state, a fixture
that does not contain the trap it claims to. `tools/tests/test_firmware_shell.py`
tests its own brace matcher for this reason.

## What is not tested

Display output, keyboard hardware, Wi-Fi association, IR carrier timing, deep
sleep and wake. These need the device.

**No Cardputer was attached while this was built**, so none of it is verified
and no device-side number in this documentation is a measurement.
[`MEASURE.md`](MEASURE.md) is the procedure for filling those in, and
`tools/sleep-probe/` answers the load-bearing question first — whether the
keyboard controller really wakes the ESP32 from deep sleep.

## The harness is tested too

`tools/mutate.py` rewrites source files in place, which makes it the one tool
whose own failure is invisible: a run killed mid-mutation leaves a broken
tree that looks like ordinary uncommitted work. `tools/tests/test_mutate.py`
pins the crash-recovery journal — in particular that it is written *before*
the source file changes, since the reverse order recovers nothing.

## The wire contract is pinned from both ends

`test_action_bodies_match_the_gateway_contract` (firmware, host) fixes the
exact JSON the device sends for every kind of intent. The literals sit
between `ACTION_BODY_CONTRACT_BEGIN` and `_END`; `tools/tests/test_action_contract.py`
extracts them from the C++ source and runs each through the gateway's
`actions.plan()`. Change a key name on either side and the other side's test
goes red — that is the point.
