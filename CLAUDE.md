# CLAUDE.md — m5-smarthome

Written for the next session, not for the archive. The pitfalls below each
cost real time.

## What this is

Firmware for an M5Stack Cardputer ADV that remote-controls the house stack on
`raspi5`, plus the Flask gateway on the Pi that it talks to. Infrastructure
context (the Pi, the apps, ports, deploy patterns) lives in
`/Users/martin/CLAUDE.md` — do not duplicate it here.

## Build, test, deploy

```bash
# firmware
cd firmware
pio test -e native            # 69 host tests, no hardware needed
pio run  -e cardputer         # compile (RAM 16.0 %, flash 31.8 %)
pio run  -e cardputer -t upload

# gateway
cd gateway
python3 -m pytest -q          # 58 tests
./deploy.sh raspi5            # tests, rsync, venv, token, systemd, health

# mutation probes — each breaks a guarantee and requires the suite to notice
python3 tools/mutate.py firmware   # 10
python3 tools/mutate.py gateway    # 6

# regenerate the IR table after touching vendor/teufel-ir-mapping.csv
python3 tools/gen_ir_table.py
```

Read the gateway token (never commit it):

```bash
ssh raspi5 'grep M5GW_TOKEN /home/pi/apps/m5-gateway/.env'
```

## Verified facts about the far end

Measured live on 2026-08-25/26. Re-check before trusting; services move.

- **Port 80 belongs to Pi-hole**, not nginx. There is no plain-HTTP route to
  the dashboard — only HTTPS on 8443.
- **The Yamaha has no JSON status.** `:5001/api/status` is 404. `:5001` is a
  transparent proxy: POST YamahaRemoteControl **XML** to
  `/api/receiver/YamahaRemoteControl/ctrl`. Volume comes back as
  `Val=-280, Exp=1` meaning −28.0 dB, step 5.
- **`Zone_B` nests inside `<Volume>`** with its own `<Lvl>` and `<Mute>`. A
  non-lazy regex or a document-wide search reads Zone B and reports the wrong
  volume with an inverted mute icon. Pinned by a test.
- **Hue `/api/lights` is 9 565 bytes**; `/api/groups` is 2 518. Groups 81–86
  are rooms, 200/201 are entertainment zones and are filtered out.
- **raspi-monitor sends decimals as strings** (`"46.85"`). House contract.
- **climate 5008, garden 5009 and weather 5011 are loopback-only.**
- The Teufel state on the Pi is an **estimate** — `server.js` flips
  `config.powered = !config.powered` after firing IR. Nothing confirms it.

## Pitfalls hit while building this

**The mutation script must restore in a `finally` — and that is not enough.**
An early ad-hoc version crashed before writing the original back and left
three mutated files in the tree. Later a *killed* run walked through the same
door, because a `finally` does not survive SIGKILL: the "rollback is a no-op"
mutation sat in `optimistic.cpp` and the next suite run blamed a guarantee no
commit had broken. `tools/mutate.py` now journals the original text to
`.mutate-journal.json` **before** touching the file and recovers on every
start; the ordering is pinned in `tools/tests/test_mutate.py`. ⚠️ The failure
looked non-deterministic (a different test named each run) purely because
`tail` hid the real one — read the whole log before calling something flaky.

**`pgrep -f` and `pkill -f` match their own command line** — including the ssh
command that carries the pattern. Checking for leftover test processes that
way reported two where there were none. Verify by port (`ss -tln`) instead.

**A hex escape in a C string swallows following hex digits.** `"K\xC3\xBCche"`
does not compile: `\xBCc` is read as one escape. Split the literal:
`"K\xC3\xBC" "che"`.

**The fuzzy matcher's Levenshtein branch was dead code.** Scores for a one-
letter typo (50) all landed *below* the acceptance threshold (60), so it never
fired. Three tests caught it at once. The thresholds are now named constants
(`kActThreshold` 60, `kHintThreshold` 50) and a one-letter slip scores 65, two
slips 55 — so one typo is acted on and two only produce a suggestion.

**`size_t` needs `<cstddef>`** in headers that otherwise only include
`<cstdint>`. The Arduino toolchain does not pull it in transitively and the
error surfaces as "has no member named len" on an unrelated struct.

**GPIO 44 is the IR LED on the Cardputer** (from
`M5Cardputer/examples/Basic/ir_nec/ir_nec.ino`) — and also appears as a
speaker pin in M5Unified, but that entry belongs to the **StampPLC**. Both
Cardputer variants use 41/43/42 for I²S. Check the board before assuming a
conflict.

**`millis()` restarts at zero after deep sleep**, so a snapshot restored from
RTC memory carries a timestamp in the future. `ageMs()` treats that as
maximally old on purpose; guessing "fresh" would show week-old values as
current.

**A launcher and esptool need *different* files.** M5Launcher/Bruce install
the **application alone** (`m5-smarthome.bin`) into an app partition; esptool
takes the **full image** (`m5-smarthome-esptool-full.bin`) at 0x0. Handing the
full image to a launcher writes a bootloader where an application belongs and
the device comes up with nothing to run — which is exactly what happened on
the first attempt here, because the docs pushed the full image as "the" file.
Tell them apart by the bytes, not the name: a valid app image has magic 0xE9
at 0 **and** the app descriptor 0xABCD5432 at offset 0x20. The launcher slot on
this hardware is 1536 KB; the build fails past it rather than shipping
something that can no longer be installed the usual way.

**The launcher's target picker looks like an error and is not one.** `Use
<name> partition` / `Remove <name>` / `Cancel` is where it asks which slot to
write into. A partition is only listed when the file fits — the loop in
`bmorcelli/Launcher`'s `src/partition_install_layout.cpp` skips entries with
`entry.size < requiredAppPartitionSize` — so seeing the dialog means the image
was accepted. Its actual rejections read `File is too big` / `file is not
valid` and never reach the picker.

**Never verify a secret's absence with `grep`.** `grep -qF "$SECRET" fw.bin`
printed nothing here while the secret sat in the file, and a scan of a public
artefact was called clean on that basis. The first diagnosis — "BSD grep exits
1 on binaries" — was **wrong**: `/usr/bin/grep` gets it right. The `grep`
being run was a shell function resolving to ugrep, which stays silent on a
binary match. That is the real lesson: `grep` is a name, not a defined tool,
and what answers depends on the shell, the PATH and the machine. Use
`tools/scan_secrets.py` (reads bytes, no subprocess, no decoding) or compare
in Python.

**A personal build lives in its own environment.** `cardputer-local` compiles
credentials in from the gitignored `secrets_local.h`; `cardputer` cannot see
that file at all, which is what keeps published binaries clean. Verified both
ways after every build, and CI scans the artefacts it is about to attach.

**⚠️ `isChange()` is CONSUMING.** It compares the current key count against
its own last-seen value and updates it, so a second call in the same pass
returns false. The shell called it once to detect an event and again to read
it — every press was detected and then read back as empty, and **no key did
anything on any screen**. It survived because the shell was declared "a thin
adapter, verified on the device" and the device was never attached. Read it
once, hand the report to `core::mapKey`. Pinned in
`tools/tests/test_firmware_shell.py`, which reads the source with comments
stripped and mutation-probed both ways.

**The "too thin to test" layer is where the worst bug lived.** Twice now: the
consuming isChange(), and a home screen whose cursor was never drawn. If a
layer cannot be unit-tested, at minimum pin its invariants against the source
— and get it onto hardware before believing it works.

**Every screen with a cursor must draw it, and Enter must do something.** The
home screen shipped with neither: arrows moved an invisible cursor and
`handleKey` had no `case Screen::Home` at all, so the device looked frozen to
anyone who reached for arrows and Enter rather than the digit shortcuts. The
digits worked the whole time, which is why it survived review. Pinned by tests
that walk every home row both ways and require Enter to act on every screen.

**Dead keys can also mean an unrecognised board.** `Keyboard_Class::begin()`
picks the TCA8418 reader for the ADV and the GPIO matrix for the original; for
anything else it installs a do-nothing reader and prints to serial only. The
firmware now says so on screen.

**Press `d` before guessing.** The diagnostics screen reports link state, the
device IP, the exact URL last requested, the last HTTP status or transport
error, request/failure counts, whether a snapshot was ever parsed, free heap
and worker stack headroom. It exists because this project spent two rounds
guessing at a device nobody could attach a cable to.

**Network settings that were wrong and looked like "the network is down":**
`WiFi.config()` without the DNS argument leaves the resolver empty;
`WiFi.setSleep(true)` parks the radio between beacons and adds ~100 ms per
exchange, which made short HTTP timeouts fire on a device that deep-sleeps
anyway; and `HTTPClient::begin(url)` without an explicit WiFiClient is
deprecated, interacts badly with setReuse, and pulled in ~117 KB of TLS code
that is never used here.

## House rules this code enforces

These come from bugs the house has already paid for. Do not relax them.

- **A failed poll is never "no data".** Last value stays, dimmed, with its age
  in the header. Both the gateway (`old`) and the firmware honour this.
- **Optimistic first.** A press moves the screen; the request follows. On
  refusal, roll back and say so.
- **Never guess a toggle.** No cached state means 409, not a coin flip.
- **Fog is interlocked twice** — once on the device, once at the gateway
  (`confirm: true` or 409). `off` is never gated.
- **IR state is unconfirmed** and is never retired by the network agreeing,
  because the Pi's own view of the Teufel is a guess too.
- **The IR table is generated**, never hand-copied. A drifted table fails
  silently.

## Known and deliberately not fixed

`CMD_MUTE` (0x28) reaches the Teufel and does nothing, while power and volume
work. The byte is mislabelled in the original capture; the cause is unknown
and documented house-wide. The firmware offers it and shows a toast saying it
is known to be ineffective. **Do not re-debug this.**

## Open

No Cardputer was attached during development, so nothing device-side is
measured: quiescent current, wake-to-usable time, frame rate and IR range are
all open. `tools/sleep-probe/` answers the sleep question first —
whether the TCA8418 interrupt really wakes the ESP32 from deep sleep — because
the whole power architecture rests on it. Procedure in `docs/MEASURE.md`.
