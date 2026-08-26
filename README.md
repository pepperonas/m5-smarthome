# m5-smarthome

<!-- badges:start -->

[![CI](https://img.shields.io/github/actions/workflow/status/pepperonas/m5-smarthome/build.yml?branch=main&style=flat-square&logo=githubactions&logoColor=white&label=CI)](https://github.com/pepperonas/m5-smarthome/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/pepperonas/m5-smarthome?style=flat-square&logo=github&label=release)](https://github.com/pepperonas/m5-smarthome/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/pepperonas/m5-smarthome/total?style=flat-square&logo=github&label=downloads)](https://github.com/pepperonas/m5-smarthome/releases)
[![Last commit](https://img.shields.io/github/last-commit/pepperonas/m5-smarthome?style=flat-square&logo=git&logoColor=white)](https://github.com/pepperonas/m5-smarthome/commits/main)
[![Licence](https://img.shields.io/github/license/pepperonas/m5-smarthome?style=flat-square)](LICENSE)

![tests: 193 passing](https://img.shields.io/badge/tests-193%20passing-brightgreen?logo=checkmarx&logoColor=white&style=flat-square)
![firmware tests: 69](https://img.shields.io/badge/firmware%20tests-69-brightgreen?style=flat-square)
![gateway tests: 71](https://img.shields.io/badge/gateway%20tests-71-brightgreen?style=flat-square)
![tool tests: 53](https://img.shields.io/badge/tool%20tests-53-brightgreen?style=flat-square)
![mutation probes: 16 caught](https://img.shields.io/badge/mutation%20probes-16%20caught-8A2BE2?style=flat-square)
![lines of code: 7 235](https://img.shields.io/badge/lines%20of%20code-7%20235-blue?style=flat-square)

![platform: ESP32-S3](https://img.shields.io/badge/platform-ESP32--S3-E7352C?logo=espressif&logoColor=white&style=flat-square)
![board: M5Cardputer ADV](https://img.shields.io/badge/board-M5Cardputer%20ADV-orange?style=flat-square)
![framework: Arduino](https://img.shields.io/badge/framework-Arduino-00979D?logo=arduino&logoColor=white&style=flat-square)
![built with: PlatformIO](https://img.shields.io/badge/built%20with-PlatformIO-F5822A?logo=platformio&logoColor=white&style=flat-square)
![C++: 4 135 lines](https://img.shields.io/badge/C%2B%2B-4%20135%20lines-00599C?logo=cplusplus&logoColor=white&style=flat-square)
![Python: 3 061 lines](https://img.shields.io/badge/Python-3%20061%20lines-3776AB?logo=python&logoColor=white&style=flat-square)

![snapshot: < 1024 B](https://img.shields.io/badge/snapshot-%3C%201024%20B-success?style=flat-square)
![PSRAM required: none](https://img.shields.io/badge/PSRAM%20required-none-success?style=flat-square)
![secrets in repo: zero](https://img.shields.io/badge/secrets%20in%20repo-zero-success?style=flat-square)
![gateway: Flask](https://img.shields.io/badge/gateway-Flask-000000?logo=flask&logoColor=white&style=flat-square)
![docs: 5 documents](https://img.shields.io/badge/docs-5%20documents-informational?logo=markdown&logoColor=white&style=flat-square)

<!-- badges:end -->

Firmware for the **M5Stack Cardputer** that turns it into a physical remote
control for a self-hosted smart home — Philips Hue, a WS2812 LED strip, a
Yamaha AV receiver, a Teufel amplifier, a fog machine and a beat-reactive
disco controller — plus a glance at indoor/outdoor climate, weather and the
server's health.

It behaves like a remote, not like a dashboard: it sleeps, a key press wakes
it, and the last known state is on screen before the radio has finished
associating.

> **Kurzfassung (DE):** Firmware für den M5Stack Cardputer als Fernbedienung
> für einen selbst gehosteten Smart-Home-Stack. Das Gerät schläft, wacht auf
> Tastendruck auf und zeigt sofort den letzten bekannten Zustand — als
> „veraltet" markiert —, während es im Hintergrund frische Daten holt. Ein
> schlanker Gateway-Dienst auf dem Raspberry Pi fasst elf Endpunkte zu **einer**
> 721-Byte-Antwort zusammen. Volltext-Befehlszeile auf Deutsch, IR als zweiter
> Steuerweg, Nebelmaschine doppelt verriegelt.

```
┌──────────────┐   one HTTP request   ┌──────────────┐   loopback   ┌─────────┐
│  Cardputer   │ ───────────────────► │   gateway    │ ───────────► │ 11 apps │
│  (ESP32-S3)  │ ◄─────────────────── │  (Flask, Pi) │ ◄─────────── │ on a Pi │
└──────┬───────┘   721 bytes of JSON  └──────────────┘              └─────────┘
       │
       └── infrared ────────────────────────────────► amplifier (works with no Wi-Fi)
```

<!-- Photo of the device on the home screen goes here. -->

---

## Why a gateway

The remote talks to exactly one service. That is not laziness, it is what the
measurements forced:

| Straight to the apps | Through the gateway |
|---|---|
| Hue's `/api/lights` alone is **9 565 bytes** of raw bridge JSON, on a board with **no PSRAM** | **721 bytes**, flat, measured |
| Eight sequential requests per refresh, each one radio time and therefore battery | **one** request |
| The Yamaha has no JSON status at all — you must speak its XML protocol | server-side, arrives as JSON |
| Three of the eleven services are bound to loopback and simply cannot be reached | all of them |
| The only LAN-wide entry point is HTTPS (port 80 belongs to Pi-hole), and TLS costs ~40 KB of heap per connection on an ESP32 | plain HTTP inside the LAN, token-authenticated |
| Every backend change breaks the firmware, and firmware is the hardest thing to update | the contract stays put; the Pi side is a `git pull` |

That last row is the real argument. Everything that moves lives on the side
you can change without a cable.

## Hardware

Built for the **M5Stack Cardputer ADV** (2025). The original Cardputer shares
the same library API and should work, but the sleep behaviour differs — see below.

| | Cardputer (original) | **Cardputer ADV** |
|---|---|---|
| Keyboard | 74HC138 GPIO matrix, scanned by the CPU | **TCA8418 over I²C, scans by itself and raises an interrupt** |
| Battery | ~120 / 1400 mAh | 1750 mAh |
| Audio | NS4168 | ES8311 + NS4150B + jack |
| Extra | — | BMI270 IMU, better antenna |

**The TCA8418 is why this design works.** It scans the key matrix on its own
and pulls its interrupt line on a press, so the ESP32 can be in deep sleep and
still be woken by the keyboard. On the original, the CPU has to scan, so the
same "sleep until pressed" trick does not apply — that build would need to
stay awake, or use a different wake source.

Pins used, each read out of the vendor library rather than guessed:

| Function | GPIO | Source |
|---|---|---|
| Keyboard interrupt (ADV) | 11 | `M5Cardputer/.../TCA8418.cpp`, `DEFAULT_TCA8418_INT_PIN` |
| Infrared LED | 44 | `M5Cardputer/examples/Basic/ir_nec/ir_nec.ino` |
| Battery sense | 10 (ratio 2.0) | `M5Unified/.../Power_Class.cpp` |

## Getting it running

### 1. The gateway, on the Pi

```bash
git clone https://github.com/pepperonas/m5-smarthome
cd m5-smarthome/gateway
./deploy.sh <your-pi-ssh-host>      # tests, rsync, venv, token, systemd
```

`deploy.sh` generates a token on first run and stores it in
`/home/pi/apps/m5-gateway/.env` with mode 600. It is never printed to a repo
and never leaves the Pi. Read it back when you set up the device:

```bash
ssh <pi> 'grep M5GW_TOKEN /home/pi/apps/m5-gateway/.env'
```

Full details, including which backends it expects and how to roll it back, are
in [`docs/GATEWAY.md`](docs/GATEWAY.md).

### 2. The firmware, on the Cardputer

**Prebuilt binaries** are attached to every
[release](https://github.com/pepperonas/m5-smarthome/releases), built by CI
from the tagged commit.

**From an SD card, like any other Cardputer firmware — the normal way:**

Copy **`m5-smarthome.bin`** to the SD card and install it from M5Launcher or
Bruce. That file is the application on its own, which is what a launcher
writes into an app partition, and what OTA takes. It uses 70 % of the 1536 KB
slot a launcher offers.

**Over USB, replacing everything on the device:**

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodem* \
  write_flash 0x0 m5-smarthome-esptool-full.bin
```

`m5-smarthome-esptool-full.bin` additionally contains the bootloader,
partition table and OTA selector at their correct offsets, so it also works
with M5Burner and web flashers that take a single image at offset 0.

**The launcher will ask where to put it.** Recent M5Launcher versions open a
target picker — `Use <name> partition` / `Remove <name>` / `Cancel` — before
installing. On a small screen with a red border it reads like a failure; it is
not. A partition only appears in that list if your file *fits*
(`entry.size < requiredAppPartitionSize` skips the ones that do not, in the
launcher's `partition_install_layout.cpp`). Pick a free slot to keep whatever
is already installed, or `Use <name> partition` to replace it. The launcher's
real failures say `File is too big` or `file is not valid` and never get as far
as asking.

> ⚠️ **Do not hand the full image to a launcher.** The launcher writes the
> file into an app partition, and a bootloader is not a valid application
> there — the device comes up with nothing to run. The launcher wants
> `m5-smarthome.bin`.

Check a download against the release's `SHA256SUMS`. Between releases, every
commit on `main` leaves a downloadable build under the
[Actions](https://github.com/pepperonas/m5-smarthome/actions) tab.

**A personal build that joins your network on first boot:**

```bash
cd firmware
cp secrets_local.h.example secrets_local.h    # gitignored
$EDITOR secrets_local.h                       # SSID, password, gateway token
pio run -e cardputer-local -t upload
```

The device then skips the setup screen entirely. The reset gesture still
works, so it is not a one-way door.

> ⚠️ **That binary contains your Wi-Fi password and gateway token in clear
> text.** Anyone holding the device or the `.bin` can read them out. Never
> publish one, never attach one to a release. The default `cardputer`
> environment cannot pick up `secrets_local.h` at all, which is why the two
> are separate environments rather than one flag.
>
> `python3 tools/scan_secrets.py --with-local` checks the tree and every built
> binary for the actual values before you push.

Or build it yourself:

```bash
cd firmware
pio test -e native              # host tests, no hardware needed
pio run  -e cardputer -t upload # compile and flash over USB-C
```

Every build writes both files (and their `.sha256`) into
`.pio/build/cardputer/`, and **fails** if the application outgrows the
launcher slot or stops being a valid app image — so an artefact that cannot be
installed the usual way never ships quietly.

### 3. First run

The device asks for four things on its own keyboard — Wi-Fi name, Wi-Fi
password, gateway address, and the token. They go into NVS. **Nothing secret
is ever compiled in**, so the build is safe to share and changing networks
needs no toolchain.

**Leave the gateway address blank** and the device finds the Pi over mDNS: the
gateway announces `_m5gw._tcp` through avahi, and the discovered address is
remembered so later boots skip the lookup. If the Pi later moves, a transport
failure triggers one re-discovery before anything is reported as broken. The
Wi-Fi password may be blank too, for an open network.

To start over: **hold any key while powering on** for two seconds — the screen
says `Taste halten fuer Reset...` and confirms when the credentials are gone.
Two seconds, so a key brushed while plugging in the cable does not wipe the
configuration.

## Keys

| Key | Home | Inside an app |
|---|---|---|
| `1`…`7` | jump to Rooms, Strip, Yamaha, Teufel, Disco, Fog, Climate | — |
| `;` `.` | — | move up / down |
| `,` `/` | `/` opens the command line | left / right (brightness, volume) |
| `+` `-` | — | brightness, volume |
| `Enter` | — | toggle the selected thing |
| `Tab` | open the command line | complete a name (in the console) |
| `` ` `` / `Esc` | — | back to Home |
| `g` | Gute Nacht (asks first) | — |
| `a` | everything off | — |
| `u` | over-the-air update mode | — |
| `w` | — | Teufel only: switch between network and infrared |
| `m` | — | Yamaha / Teufel: mute |
| `i` | — | Yamaha / Teufel: step through inputs |
| `e` | — | Strip: step through the 13 effects |
| `o` | — | Disco: step through the 6 modes |

The arrows are the ones printed on `;` `.` `,` `/` — the Cardputer has no
dedicated arrow keys. On the home screen there is nothing to move left or
right through, so `/` is free for the command line there.

### Updating without a cable

Press `u` on the home screen. The device shows its IP address and waits:

```bash
cd firmware
pio run -e cardputer -t upload --upload-port <the address on screen>
```

It is an explicit mode rather than a background listener, because this device
sleeps after 30 s with its radio off — an always-on OTA server would be either
asleep when you wanted it or the reason the battery did not last. `Esc` leaves
the mode.

### The command line

Type instead of navigating. Matching is fuzzy, folds umlauts and forgives one
typo:

```
wohnzimmer aus      turn the living room off
wohn an             prefixes are enough
küche aus           küche, kueche and kuche all work
flur 50             set the hallway to 50 %
alles aus           the whole flat
nacht               goodnight macro (asks first)
disco               toggle the disco lights
nebel               fog machine (asks first, twice over)
lauter / leiser     receiver volume
```

Two typos deliberately do **not** act — they suggest. Guessing which room
somebody meant after two slips is how you switch off the wrong floor.

## Honesty rules the display follows

These are not decoration; each one comes from a bug this house has already
paid for.

- **A failed poll is never rendered as "no data".** The last value stays on
  screen, dimmed, with `Stand N s alt` in the header. A Wi-Fi hiccup must not
  look like a dead house.
- **A press moves the screen immediately.** The request follows behind it. If
  the gateway refuses, the screen rolls back and says so.
- **The Teufel is always marked with `~`.** The Pi has no feedback path to
  that amplifier — it flips a flag after firing infrared and hopes. That is an
  estimate and the display says so on every frame.
- **Infrared is marked unconfirmed.** Nothing acknowledges an IR burst. The
  state stays flagged until a network poll independently agrees, and even then
  the Teufel keeps its tilde, because the Pi's own view is a guess too.
- **The fog machine asks twice.** Once on the device, once at the gateway,
  which refuses `fog/on` without `confirm: true`. Switching it *off* is never
  gated.

## Tests

```bash
cd firmware && pio test -e native        # 69 tests, no hardware needed
cd gateway  && python3 -m pytest -q      # 70 tests
python3 -m pytest tools/tests -q         # 36 tests (image checks, generators)
python3 tools/mutate.py firmware         # 10 mutations, all must be caught
python3 tools/mutate.py gateway          # 6 mutations, all must be caught
python3 tools/badges.py --check          # README badges are not stale
python3 tools/scan_secrets.py --with-local   # no credentials in tree or binaries

./tools/preflight.sh                     # all of the above, in the right order
```

The order in `preflight.sh` is load-bearing: adding a test changes the
test-count badge, so the badges are regenerated *before* the drift check, not
after. Running the steps by hand got that wrong once.

Everything deterministic — JSON parsing, the command matcher, the overlay
logic, NEC framing, the timing policy — lives in hardware-free modules under
`firmware/lib/core` and runs on the host. The display, keyboard, Wi-Fi and IR
timing are thin adapters on top and are verified on the device instead.

The mutation harness deliberately breaks each load-bearing guarantee (remove
the fog interlock, let a broken reply wipe the last good state, make an IR
press count as confirmed, …) and fails if the suite does not go red. A test
you have never seen fail is not an assurance.

## Layout

```
firmware/lib/core/   pure, tested on the host: dash, command, optimistic,
                     netplan, ui_state, ir_nec, ir_teufel (generated)
firmware/src/        Arduino shell: display, keyboard, Wi-Fi task, IR, NVS
gateway/m5gw/        Flask app: aggregate (pure), actions (pure), backends (I/O)
tools/               IR table generator, mutation harness, sleep probe
vendor/              the canonical Teufel IR mapping, copied with provenance
docs/                architecture, API contract, gateway operations, measuring
```

## Troubleshooting

| Symptom | Cause |
|---|---|
| Header says `kein WLAN`, values dimmed | Expected during a reconnect. Presses are queued and sent when the link returns. |
| `abgelehnt` after pressing Enter on fog | The gateway's interlock. Confirm on the device first. |
| Mute does nothing on the Teufel | Known and unexplained: byte `0x28` reaches the box and has no effect, while power and volume work. Not a firmware fault. |
| Everything stale, gateway healthy | Check the token: a wrong one gives 401 and the device keeps the old snapshot rather than blanking. |
| Launcher shows `Use … partition` / `Remove …` / `Cancel` | Not an error — that is the target picker. Choose a slot; the file already passed its size and validity checks or the dialog would not have appeared. |
| Launcher installs it, device shows nothing / boot-loops | The full image was given to the launcher. Use `m5-smarthome.bin`, then reflash via USB once to recover. |
| Setup screen on every boot | NVS did not persist — check that the partition table matches the 8 MB flash. |
| Never finds the gateway | mDNS may be filtered on the network. Type the Pi's IP during setup instead; the field exists for exactly this. |

## Status

The gateway is deployed and verified against the live house. The firmware
compiles (RAM 17.3 %, flash 33.0 %) and its pure core is covered by host
tests. **On-device measurements — quiescent current, wake-to-usable time,
frame rate, infrared range — were not taken: no Cardputer was attached while
this was built.** [`docs/MEASURE.md`](docs/MEASURE.md) is the procedure for
filling those in, and `tools/sleep-probe/` is a ready-to-flash sketch that
answers the sleep question first — `sleep-probe.bin` is attached to releases
too, so it needs no toolchain.

## License

MIT — see [LICENSE](LICENSE).
