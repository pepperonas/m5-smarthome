# Pitfalls

Things that cost real time here, written down so they cost you less. Most are
not bugs in anyone's code — they are behaviours that look like something else.

Each one is pinned by a test, named at the end of the entry, so it cannot come
back quietly.

---

## M5Cardputer

### `isChange()` is consuming

The single worst bug in this project. `Keyboard_Class::isChange()` compares the
current key count against its own remembered value **and updates it**:

```cpp
bool Keyboard_Class::isChange() {
    uint8_t current_size = keyList().size();
    if (_last_key_size != current_size) {
        _last_key_size = current_size;      // consumed
        return true;
    }
    return false;
}
```

Calling it once to notice a press and again to read it means the second call
returns `false`. Every key press was detected and then read back as empty —
**nothing on the device responded to any key, on any screen.**

Read it once per pass and carry the result:

```cpp
core::KeyReport r;
r.changed = M5Cardputer.Keyboard.isChange();     // exactly once
r.pressedCount = M5Cardputer.Keyboard.isPressed();
if (!core::hasKeyEvent(r)) return false;
```

*Pinned by `tools/tests/test_firmware_shell.py`.*

### The arrows are `; . , /` and need no Fn

The Cardputer has no dedicated arrow keys. The arrows are printed on those four
keys, and the library reports them as exactly those characters — `fn` is
reported separately and does not change `word`.

`Enter` arrives with an **empty** `word`: it is a flag on `KeysState`, not a
character. Requiring a character before handling a report swallows every Enter.

*Pinned by `firmware/test/test_core/test_keymap.cpp`.*

### An unrecognised board gives you silently dead keys

`Keyboard_Class::begin()` picks the TCA8418 reader for the ADV and the GPIO
matrix for the original. For anything else it installs a do-nothing reader and
reports it **over serial only** — leaving a device whose keys simply do not
work, with no explanation on screen. Check `M5.getBoard()` yourself and say so.

### The two variants differ where it matters

| | Cardputer | Cardputer ADV |
|---|---|---|
| Keyboard | 74HC138 matrix, CPU scans it | **TCA8418 over I²C, scans itself, raises an interrupt** |
| Wake from deep sleep on a key | not this way | **yes, ext0 on GPIO 11** |

The ADV's controller is what makes a sleeping remote possible at all. A design
that assumes it will not work on the original.

### Pins, and one that looks like a conflict

| Function | GPIO | Where it is written down |
|---|---|---|
| Keyboard interrupt (ADV) | 11 | `M5Cardputer/.../TCA8418.cpp`, `DEFAULT_TCA8418_INT_PIN` |
| Infrared LED | 44 | `M5Cardputer/examples/Basic/ir_nec/ir_nec.ino` |
| Battery sense | 10, ratio 2.0 | `M5Unified/.../Power_Class.cpp` |

GPIO 44 also appears as a speaker pin in M5Unified — but that row belongs to
the **StampPLC**. Both Cardputer variants use 41/43/42 for I²S. Check which
board a table row is for before believing in a conflict.

---

## Flashing

### A launcher and esptool need *different* files

An SD-card launcher writes the file you pick into an **app partition**, so it
must be an application. `esptool` writes a **full image** from `0x0`.

Both start with magic `0xE9`, so the first byte cannot tell them apart. The
app descriptor at offset `0x20` can:

```python
is_app = head[0] == 0xE9 and int.from_bytes(head[0x20:0x24], "little") == 0xABCD5432
```

Handing the full image to a launcher installs a bootloader where an
application belongs, and the device comes up with nothing to run.

*Pinned by `tools/tests/test_image_check.py` and enforced at build time.*

### The launcher's target picker is not an error

Recent M5Launcher versions ask `Use <name> partition` / `Remove <name>` /
`Cancel` before installing. On a small screen with a red border this reads
like a failure. It is not — and a partition only appears in that list if your
file **fits**: the loop in the launcher's `partition_install_layout.cpp` skips
entries with `entry.size < requiredAppPartitionSize`. Its real refusals say
`File is too big` or `file is not valid` and never reach the picker.

The app slot on this hardware is 1536 KB, and partitions are aligned, so it is
the *aligned* size that has to fit.

---

## ESP32 networking

### Three settings that present as "the network is down"

- **`HTTPClient::begin(url)` without an explicit `WiFiClient`** is deprecated,
  interacts badly with `setReuse` across differing requests, and pulled in
  ~117 KB of TLS code this project never uses. Pass a client:
  `WiFiClient c; http.begin(c, url);`
- **`WiFi.setSleep(true)`** parks the radio between beacons, adding roughly
  100 ms per exchange. On a device that deep-sleeps between uses this saves
  power you were not spending, and the added latency is enough to make a
  short connect timeout fire.
- **`WiFi.config()` without the DNS argument** leaves the resolver empty.
  Anything that later uses a hostname fails in a way that looks like no
  connectivity at all.

### `millis()` restarts after deep sleep

State restored from RTC memory carries a timestamp in the *future*. Treat that
as maximally old rather than fresh — guessing "fresh" presents week-old values
as current, which is the dangerous direction.

*Pinned by `firmware/test/test_core/test_main.cpp`.*

---

## Tooling

### Never verify a secret's absence with `grep`

```bash
grep -qF "$PASSWORD" firmware.bin && echo LEAK    # printed nothing
```

…while the password was in the file. The first diagnosis was wrong in an
instructive way: `/usr/bin/grep` (BSD grep) handles this correctly. The `grep`
actually running was a shell function resolving to **ugrep**, which stays
quiet on a binary match.

The lesson is not about an implementation. `grep` is a name, not a defined
tool, and what answers depends on the shell, the `PATH` and the machine. Read
the bytes: `tools/scan_secrets.py` does, with no subprocess and no decoding.

### `pgrep -f` and `pkill -f` match their own command line

Including the `ssh` invocation that carries the pattern. Checking for leftover
processes that way reported two where there were none. Check by port instead.

### A hex escape swallows following hex digits

`"K\xC3\xBCche"` does not compile: `\xBCc` is read as one escape. Split the
literal: `"K\xC3\xBC" "che"`.

### Text checks must run against comment-free source

A comment explaining a removed defect contains the very words a search looks
for, so a check that reads the raw file passes while the defect is back. Strip
comments first — and mutate every such check once to watch it fail.

### The baked config only seeded an empty NVS — and an empty host is "valid"

`store::load()` accepts a configuration without a host (mDNS can supply one),
so a device set up once — by hand, or by an earlier local build whose
`secrets_local.h` had no host — held on to that configuration through every
reflash. The seed ran only when `load()` failed, which it never did again.
The result: Wi-Fi connects, the screen fills with placeholders, and nothing
in the newer binary can reach the device.

Now a fingerprint of the baked values lives in NVS and the seed fires
whenever the compiled-in set *changes* (`core::configFingerprint`, pinned and
mutation-probed). Corollary for diagnosis: the screen shows the configured
target (`GW <host>:<port>  Token ja/FEHLT`) before the first request, because
a device aimed at nothing produces no URL, no HTTP status and no error.

### A dead end that returns silently looks like "no polling at all"

`runJob()`'s early returns — Wi-Fi down, mDNS discovery failed — set no error
and bumped no counter. The diagnostics screen then showed `Abrufe 0` on a
device that was polling constantly. Every abandoned request now counts as a
failed fetch and names its reason.

---

## Testing

### The "too thin to test" layer is where the worst bugs live

Twice here: the consuming `isChange()`, and a home screen whose cursor was
never drawn while Enter had no handler at all. Both sat in code declared "a
thin adapter, verified on the device" — and no device was attached.

If a layer genuinely cannot be unit-tested, pin its invariants against the
source, and get it onto hardware before believing it works.

### A test you have never seen fail is not an assurance

`tools/mutate.py` breaks each load-bearing guarantee on purpose and requires
the suite to go red. It restores every file in a `finally` — an earlier ad-hoc
version crashed mid-run and left three mutated files in the tree, which is
exactly the failure it now guards against.

### A `finally` does not survive SIGKILL

The same door, opened again: a killed run left the "rollback is a no-op"
mutation sitting in `optimistic.cpp`. The next suite run then reported a
broken guarantee that no commit had broken, and the file looked like an
ordinary uncommitted edit in `git status`.

Cleanup you can be killed out of is not cleanup. The original text is now
journalled to `.mutate-journal.json` **before** the source file is touched,
and every run recovers what a dead one left behind. The ordering is the whole
mechanism and is pinned by a test; a journal written after the mutation would
leave exactly the same orphan.

Diagnosis note, because it cost a wrong turn: the failure looked
*non-deterministic* — a different test named in each run. It was not. `tail`
on the output hid the real failure, which was the same one every time. Read
the whole log before calling something flaky.
