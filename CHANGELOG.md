# Changelog

## Unreleased

Three defects found by putting the firmware on real hardware, and the tooling
added so the next one is found by a test instead.

### Fixed
- **No key did anything, on any screen.** The vendor's `isChange()` is
  consuming — it updates its own last-seen state and returns false on a second
  call — and the shell called it twice per loop, so every press was detected
  and then read back as empty.
- **The home screen ignored arrows and Enter.** Its cursor was never drawn and
  `handleKey` had no case for it; only the digit shortcuts worked, which is
  what I had been testing with.
- **Three network settings that present as "the network is down":**
  `HTTPClient::begin(url)` without an explicit client (deprecated, fights
  `setReuse`, and pulled in ~117 KB of unused TLS — flash fell 33.1 % → 29.5 %),
  `WiFi.setSleep(true)` adding ~100 ms per exchange on a device that
  deep-sleeps anyway, and `WiFi.config()` without a DNS argument.

### Added
- **Diagnostics screen** on `d`: link state, IP, last URL, HTTP status or
  transport error, request/failure counts, whether a snapshot ever parsed,
  free heap, worker stack headroom. It sends nothing.
- Inputs, strip effects and disco modes reachable from the device (`i`, `e`,
  `o`) — the gateway accepted them all along.
- mDNS discovery of the gateway, and a reset gesture that actually exists.
- Over-the-air updates as an explicit mode on `u`.
- A personal build environment that compiles credentials in from a gitignored
  header; the published environment cannot see that file.
- **Documentation drift tests**: every key the firmware handles must appear in
  the README table, every gateway action must have a row in `docs/API.md`,
  ports and enumerations must agree between firmware and gateway, internal
  anchors and cross-document links must resolve, and no test count may be
  written into prose.
- `docs/TESTING.md` and `docs/PITFALLS.md`.
- A secret scanner that reads bytes rather than shelling out to `grep`, and
  `tools/preflight.sh`, which runs the whole gate in the order that makes it
  true.

## 0.1.0 — 2026-08-26

First release: gateway deployed and verified against the live house, firmware
complete and building, pure core covered by host tests.

### Gateway
- `GET /api/dash` aggregates 11 house endpoints into one flat snapshot,
  measured at **721 bytes** live (Hue's `/api/lights` alone is 9 565).
- Parallel fetch with hard per-backend timeouts; a hanging device degrades to
  a stale marker instead of delaying the snapshot.
- `err` / `old` distinguish "never arrived" from "last known value", so a
  client can dim rather than blank. Verified live by taking a backend down
  under a running gateway.
- Yamaha XML parsed server-side — the receiver has no JSON status endpoint.
- Named write actions only; no generic pass-through.
- Fog ignition requires an explicit `confirm: true` (409 without it); fog
  `off` is never gated.
- Toggles resolve from cached state and refuse with 409 rather than guessing.
- Shared-secret auth, failing closed: no token means no start and no access.
- systemd unit with a sandboxed non-root service on port 5010.

### Firmware
- Home screen, room list, and detail screens for strip, receiver, amplifier,
  disco, fog and climate; inputs, strip effects and disco modes step on a key.
- Digit keys jump straight into an app; typed command line with fuzzy German
  matching, umlaut folding and Tab completion.
- Optimistic rendering with rollback on refusal; failed polls never render as
  "no data".
- Teufel state marked as an estimate on every frame; IR-driven state marked
  unconfirmed.
- Infrared as a second control path, table generated from the canonical CSV.
- Deep sleep with wake on the TCA8418 keyboard interrupt; fast reconnect from
  a remembered BSSID; last snapshot drawn from RTC memory before the radio is up.
- Credentials typed on the device into NVS, never compiled in.
- Finds the gateway over mDNS (`_m5gw._tcp`) when no address was typed;
  re-discovers once after a transport failure.
- Credentials wiped by holding a key for two seconds at power-on.
- Over-the-air updates as an explicit mode on `u`, never a background listener.
- Builds at RAM 17.3 %, flash 33.0 %.

### Tests
- 69 host tests for the firmware core, 58 for the gateway.
- Mutation harness: 10 firmware and 6 gateway mutations, all caught.

### Not measured
No Cardputer was attached during development. Quiescent current, wake-to-usable
time, frame rate and infrared range are unmeasured and documented as such —
see `docs/MEASURE.md`.
