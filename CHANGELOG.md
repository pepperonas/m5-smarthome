# Changelog

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
- Builds at RAM 16.0 %, flash 31.8 %.

### Tests
- 69 host tests for the firmware core, 58 for the gateway.
- Mutation harness: 10 firmware and 6 gateway mutations, all caught.

### Not measured
No Cardputer was attached during development. Quiescent current, wake-to-usable
time, frame rate and infrared range are unmeasured and documented as such —
see `docs/MEASURE.md`.
