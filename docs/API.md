# Gateway API

The contract between the Cardputer and the Raspberry Pi. Everything the remote
knows about the house arrives through these three endpoints.

Base URL: `http://<pi>:5010`. Plain HTTP, LAN only — see
[ARCHITECTURE.md](ARCHITECTURE.md#why-not-tls) for why, and note that the
token is what actually gates access.

## Authentication

Every endpoint except `/api/health` needs the shared secret, in any of:

```
Authorization: Bearer <token>
X-Token: <token>
?t=<token>
```

The query form exists because it is the cheapest thing for a microcontroller
and for `curl`. Comparison is constant-time.

**The service fails closed.** With no `M5GW_TOKEN` configured it refuses to
start, and if one is somehow empty at runtime every request gets 401. An empty
secret never means "no secret required" — this gateway can switch a 220 V fog
machine.

| Status | Meaning |
|---|---|
| 401 | missing or wrong token |
| 400 | malformed action, unknown effect/input, value out of range |
| 404 | unknown target or macro |
| 409 | the action needs something first: fog confirmation, or a toggle whose current state is unknown |
| 502 | the action was well-formed but a backend did not answer |

---

## `GET /api/health`

Unauthenticated liveness. Discloses nothing.

```json
{"ok": true, "service": "m5-smarthome-gateway"}
```

## `GET /api/dash`

The whole house in one small object. Add `?force=1` to bypass the cache.

Measured live: **721 bytes**, 30 ms warm, 340 ms cold (the weather backend is
the slow one), 1.2 ms when served from cache.

```json
{
  "t": 1787714151,
  "hue": {"g": [{"i": 81, "n": "Wohnzimmer", "on": true, "b": 174}], "on": 5},
  "lw": {"on": false, "b": 255, "fx": "iris_warn", "warn": true},
  "yam": {"on": true, "raw": -280, "vol": -28.0, "mute": false, "in": "Spotify"},
  "tf": {"on": true, "vol": 29, "mute": true, "in": "AUX", "est": true},
  "fog": {"on": false, "tank": 48, "ml": 120},
  "disco": {"on": false, "bpm": 0, "spl": 31.5, "mode": "rainbow"},
  "clima": {"in": {"t": 22.7, "h": 54}, "out": {"t": 16.3, "h": 65}},
  "wx": {"t": 14.1, "ic": "04n", "d": "Bedeckt", "hi": 26, "lo": 14},
  "pi": {"cpu": 2.6, "tmp": 47.4, "mem": 15.0}
}
```

Keys are short on purpose: the payload crosses Wi-Fi to a device without
PSRAM, and every byte is radio time, which is battery.

### Fields

| Path | Type | Meaning |
|---|---|---|
| `t` | int | server unix time when the snapshot was built |
| `hue.g[].i` | int | Hue group id (stable: 81 living room … 86 garden) |
| `hue.g[].n` | string | room name, leading sort-dot stripped |
| `hue.g[].on` | bool | any lamp in the room is on |
| `hue.g[].b` | int | group brightness, 0…254 |
| `hue.on` | int | how many rooms are lit |
| `lw.on` / `lw.b` / `lw.fx` | bool/int/string | strip power, brightness 0…255, current effect |
| `lw.warn` | bool | present only when strip-warn owns the strip; the remote must not paint over it |
| `yam.raw` | int | receiver volume in its own units, −280 = −28.0 dB, step 5 |
| `yam.vol` | float | the same value in dB, for display |
| `yam.in` | string | selected input, as the receiver names it |
| `tf.est` | bool | **always true.** The Pi estimates this state by flipping a flag after firing IR; nothing confirms it |
| `fog.tank` / `fog.ml` | int | tank level in percent and millilitres |
| `disco.spl` | float | sound pressure level as the disco controller reports it |
| `clima.*.old` | int | present only when a sensor reading is older than 10 minutes, value in seconds |
| `wx.ic` | string | OpenWeather icon code |
| `pi.*` | float | CPU %, SoC temperature, memory % |

### `err` and `old` — the important pair

```json
{"err": ["yam", "wx"], "old": ["lw"]}
```

- **`err`** lists sources that produced nothing. Their objects are absent from
  the payload entirely — no zeros, no placeholders, no invented values.
- **`old`** lists sources whose object *is* present but is a **last known
  value**: the gateway had it before and could not refresh it this round.

That distinction is the entire point. A client must render an `old` source
dimmed and labelled, never blank it — otherwise every network hiccup looks
like a broken device. This has bitten this house before, on a chart that
erased itself whenever a poll timed out.

Verified live by taking a backend down under a running gateway: the value
stayed and `old: ["wx"]` appeared.

### Caching

A snapshot is reused for `M5GW_CACHE_TTL` (default 1 s), so a burst of polls
costs one round of backend fetches. Slow-moving sources — weather, climate,
Pi health — are refreshed on their own `M5GW_SLOW_TTL` clock (default 60 s).

All backends are fetched **in parallel** with a hard `M5GW_FETCH_TIMEOUT`
(default 1.5 s). One hanging device cannot delay the snapshot; it degrades to
`old` instead. A powered-down Yamaha once froze a 2 s dashboard refresh loop
in this house, which is why this is not negotiable.

---

## `POST /api/act`

One named action per request. There is deliberately **no generic
pass-through** — a proxy that forwards arbitrary paths to six unauthenticated
loopback services is a hole punched straight through all of them.

```json
{"target": "hue", "action": "toggle", "group": 81}
```

Reply:

```json
{"ok": true, "applied": {"hue": {"group": 81, "on": false}}}
```

`applied` is what the client may optimistically assume. It is present **even
when `ok` is false**, so the remote knows exactly which assumption to roll
back rather than having to re-derive it.

### Actions

| target | action | parameters | notes |
|---|---|---|---|
| `hue` | `on` `off` `toggle` | `group` | `toggle` resolves against the cached snapshot |
| `hue` | `bri` | `group`, `bri` 1…254 | the controller switches a dark group on |
| `hue` | `ct` | `group`, `ct` 153…500 | colour temperature |
| `hue` | `all` | `on` | every lamp in the house |
| `lw` | `on` `off` `toggle` | — | |
| `lw` | `bri` | `bri` 0…255 | |
| `lw` | `speed` | `speed` 1…100 | |
| `lw` | `effect` | `effect` | 13 names; `iris_warn` is refused on purpose |
| `lw` | `color` | `r` `g` `b` 0…255 | |
| `yam` | `on` `off` `toggle` | — | |
| `yam` | `vol` | `step` −40…40 **or** `db` | one step is 0.5 dB |
| `yam` | `mute` | optional `on` | |
| `yam` | `input` | `input` | whitelist queried from the receiver |
| `tf` | `power` | — | the amplifier only understands toggle |
| `tf` | `vol` | `step` −20…20 | |
| `tf` | `mute` | — | reaches the box and does nothing; see below |
| `tf` | `input` | `input` | AUX, LINE, OPTICAL, USB, BLUETOOTH |
| `fog` | `on` | **`confirm: true`** | 409 without it |
| `fog` | `off` | — | never gated |
| `disco` | `on` `off` `toggle` | — | |
| `disco` | `mode` | `mode` | rainbow, party, random, pulse, solid, strobe |
| `disco` | `bri` | `bri` 10…100 | |
| `macro` | `goodnight` `alloff` `wake` | — | several backends in one request |

### Refusals worth knowing about

**Fog.** `{"target":"fog","action":"on"}` returns **409** unless the body
carries `"confirm": true`. The value must be the boolean `true` — `"yes"`,
`1` and `"true"` are all rejected. There is no `toggle` for fog: with a heater
you always say which direction you mean. `off` is never gated, because the
one thing that must always work is stopping it.

**Toggles without state.** If the gateway has no cached snapshot it answers
**409 "state unknown, cannot toggle"** rather than picking a direction. A
remote that flips the wrong way is worse than one that admits it does not know.

**Yamaha volume steps** need a known level for the same reason, and answer 409
without one.

### Macros

`goodnight` and `alloff` switch off all Hue lamps, the strip and the disco,
**switch the fog machine off**, and power down the receiver if it is on. They
never start anything. `wake` turns the lamps on and deliberately does not
touch the fog machine either.

---

## Errors from backends

If a backend refuses or times out, the action returns **502** with which
target failed:

```json
{"ok": false, "applied": {"lw": {"on": false}}, "results": [{"target": "lw", "ok": false}]}
```

The gateway drops its cached snapshot after every write, so the next poll
reflects reality rather than the assumption.
