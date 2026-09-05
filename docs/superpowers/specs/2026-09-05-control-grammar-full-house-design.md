# One grammar for the whole house — design

*2026-09-05. Status: draft for review.*

The remote reaches Hue rooms, the strip, the receiver, the Teufel, disco, fog
and climate today, but only their front doors: on/off, a level, one cycled
value. The house apps expose about forty-five more functions — colours,
scenes, EQ, thresholds, the dB history, the LED matrix, forecasts. This
design makes all of them reachable on a 240×135 panel with a 56-key keyboard
without inventing a key per function, and keeps every decision about *what a
key does* in host-tested code.

## 1. Goals and non-goals

**Goals**

- Every function listed in §3 is reachable with the same five keys on every
  screen, and discoverable by scrolling — no cheat sheet.
- A press moves the screen immediately (optimistic overlay), the request
  follows, a refusal rolls back and says so. Existing machinery, extended.
- The honesty rules hold everywhere: stale is marked, estimates are marked,
  a failed poll never blanks.
- Everything that decides behaviour — which controls a screen has, their
  bounds, steps, what they send — is a table in `lib/core`, tested on the
  host and mutation-probed. The shell only draws.
- Wake-to-usable stays under a second: the snapshot grows, but stays under
  one radio frame; anything larger is fetched on demand, only while its
  screen is open.

**Non-goals** (deliberately out, with the reason)

- Disco lamp picker, white-mix sliders, salience CSV, effect builder,
  timers/schedules — multi-select lists and nine-slider panels do not fit
  this panel; the phone is the right tool.
- Yamaha Zone 2/3, DSP programs, party mode — the receiver lives in one room.
- Fog custom RF codes, duration bursts — the machine times its own bursts;
  a remote that can send arbitrary codes to a heater is a liability.
- Hue per-lamp control — rooms are the unit the house uses everywhere.

## 2. The grammar

Every app screen is a **list of controls**. Nine control kinds, one renderer,
one key contract.

| Kind | Looks like | `,` `/` (or `-` `+`) | `Enter` |
|---|---|---|---|
| Toggle | `Strom          [an]` | flips | flips |
| Level | `Pegel   ▮▮▮▮▮▯▯▯  -28.0 dB` | ± one step, sends | — |
| Choice | `Eingang   ◂ Spotify ▸` | cycles, sends | — |
| Picker | `Szene     ◂ Entspannen ▸` | cycles, sends nothing | sends the shown value |
| Stepper | `Bass          ◂ ▸` | sends −/+ (no state to show) | — |
| Color | `Farbe   ■■■■■■■■ ○` | moves the frame, sends | — |
| Action | `Nachfuellen: voll     ▸` | — | fires |
| Link | `Szenen                ▸` | — | opens a sub-list |
| Readout | `Tank        48 % (120 ml)` | not selectable | — |

Navigation keys, unchanged from today: `;` `.` move the cursor, `` ` ``/Esc
go back one level, `Tab` opens the console, digits jump from Home. New:
**`Space` flips the screen's primary toggle** (the first Toggle in the list;
on the room list, the highlighted room) from anywhere on that screen — the
one-key "power" every remote has.

The existing letter accelerators stay (`m` mute, `i` input, `e` effect,
`o` mode, `p` power, `w` path) and now do two things: move the cursor onto
that control *and* act, so the user sees what changed.

Readouts are skipped by the cursor. Lists longer than the visible rows
scroll around the cursor (as the room list does today) with a 2-px scroll
mark on the right edge.

Choice vs. Picker is a deliberate split: an input or effect should switch as
you step through it (immediate, reversible, that is what a remote feels
like); a scene or a mood sequence should not fire eight times on the way to
the one you meant.

A Stepper exists for the Teufel EQ and balance only: the box is write-only
over IR, so there is no level to draw. The label carries a `~` like every
other Teufel value.

## 3. Screens

Home gains an eighth row and moves to 13-px rows (8 × 13 = 104 ≤ 108 px of
content).

```
1 Raeume   2 Strip   3 Yamaha   4 Teufel   5 Disco   6 Nebel   7 Klima   8 dB
```

Steps, ranges and wire actions below are the contract; the gateway
whitelists exactly these.

### 3.1 Räume (Hue)

List of rooms. Each row: name, right side `aus` or `62 %`, and a colour dot
from the gateway's `c` field. `,` `/` adjust brightness ±12 % straight from
the list (the fast path stays); `Space` toggles the highlighted room;
`Enter` opens the room.

Room screen:

| Control | Kind | Range / values | Sends |
|---|---|---|---|
| An | Toggle | | `hue on/off {group}` |
| Helligkeit | Level | 1–254, step 30, shown % | `hue bri {group, bri}` |
| Farbe | Color | 8 presets (§5) | `hue color {group, preset}` |
| Farbton | Level | 0–359°, step 15, wraps | `hue hue {group, hue:0–65535, sat:254}` |
| Weisston | Level | ct 153–500, step 25, shown warm…kalt | `hue ct {group, ct}` |
| Szenen | Link | bridge scenes of this group, fetched on demand | opens Picker list → `hue scene {group, scene}` |

Bottom of the room list, after the rooms:

| Control | Kind | Sends |
|---|---|---|
| Alle | Toggle | `hue all {on}` |
| Bewegungsmelder | Toggle | `hue pir {on}` (POST /api/pir/toggle; the gateway resolves direction from `pir.running`) |
| Stimmung | Picker (20) | `hue mood {name}` → POST /api/mood-scenes/<name> {lights:"all"} |

### 3.2 Strip (Lichtwerk)

| Control | Kind | Range | Sends |
|---|---|---|---|
| Strom | Toggle | | `lw on/off` |
| Helligkeit | Level | 0–255, step 32 | `lw bri` |
| Effekt | Choice | 13 (no `iris_warn`) | `lw effect` |
| Tempo | Level | 1–100, step 10 | `lw speed` |
| Farbe | Color | 8 presets as RGB | `lw color {r,g,b}` |
| Farbton | Level | 0–359°, step 15 | `lw color {r,g,b}` from hue, sat 1, val 1 |

While `warnOwned` is set, every control shows dimmed and refuses with the
existing toast — the strip belongs to strip-warn.

### 3.3 Yamaha

| Control | Kind | Range | Sends |
|---|---|---|---|
| Strom | Toggle | | `yam on/off` |
| Pegel | Level | −80…−20 dB, step 1 dB (2 raw steps) | `yam vol {step}` (existing) |
| Eingang | Choice | 8 (existing list) | `yam input` |
| Stumm | Toggle | | `yam mute` |
| Bass | Level | −6…+6 dB, step 0.5 | `yam bass {db}` |
| Hoehen | Level | −6…+6 dB, step 0.5 | `yam treble {db}` |
| Extra Bass | Toggle | | `yam xbass {on}` (sends `Auto`/`Off` — the RX-V577 rejects `On`) |
| Sleep | Choice | aus/30/60/90/120 | `yam sleep {min}` |
| Szene | Picker | 1–4 | `yam scene {n}` |

### 3.4 Teufel

Everything estimated, everything marked `~`. `Weg` decides whether a control
goes through the Pi's bridge or the device's own IR LED — the IR table
carries every code below (power, volume, mute, inputs, bass/mid/treble,
balance, left/right).

| Control | Kind | Range | Sends (Netz) | IR |
|---|---|---|---|---|
| Weg | Choice | Netz / IR (blind) | local | — |
| Strom | Toggle | | `tf power` | POWER |
| Lautstaerke | Level | 0–50, step 1 | `tf vol {step}` | VOL_UP/DOWN |
| Eingang | Choice | 5 | `tf input` | AUX/LINE/OPT/USB/BLUETOOTH |
| Stumm | Toggle | | `tf mute` (known quirk toast) | MUTE |
| Bass / Mitten / Hoehen | Stepper | | `tf eq {type, dir}` | BASS/MID/TREBLE_UP/DOWN |
| Balance | Stepper | | `tf balance {dir}` | BAL_LEFT/RIGHT |
| Matrix | Choice | off db pegel bpm smiley vu heart spektrum welle temp humidity clock analog | `tf matrix {mode}` | — |
| Iris | Toggle | | `tf iris {on}` | — |

Steppers claim no overlay (nothing to show); they toast `Bass +`.

### 3.5 Disco

| Control | Kind | Range | Sends |
|---|---|---|---|
| Lichter | Toggle | | `disco toggle` (master, respects targets) |
| Modus | Choice | 6 | `disco mode` |
| Theme | Choice | 7 | `disco theme` |
| Helligkeit | Level | 10–100, step 10 | `disco bri` |
| Empfindlichkeit | Level | 0–100, step 10 | `disco sens` (turns auto off, server-side) |
| Auto-Empf. | Toggle | | `disco autosens {on}` |
| Ziel | Choice | Hue / Strip / Beide | `disco targets` |
| Farbe | Color | 8 presets → `#rrggbb` | `disco color` (label notes: nur solid/strobe) |
| dB-Analyse | Link | | opens 3.8 |

### 3.6 Nebel

| Control | Kind | Sends |
|---|---|---|
| Nebel | Toggle | `fog on` (confirm, existing) / `fog off` |
| Tank | Readout | `48 % (120 ml), reicht ~9 min` from tank fields |
| Nachfuellen: voll | Action | `fog refill {full:true}` |
| Nachfuellen: +100 ml | Action | `fog refill {ml:100}` |

### 3.7 Klima und Wetter

Readouts: Innen, Garten, Wetter jetzt (temp, desc, hi/lo), then

| Control | Kind | |
|---|---|---|
| Vorhersage | Link | opens a readout screen: 4 slots `14h  18.2  Regen 40%`, 5 days `Sa  14/23  bewoelkt`, Zambretti text, rain-now label — fetched on demand |
| Pi | Readout | `2 % CPU  48 C  16 % RAM  Luefter 1200/min` |

### 3.8 dB-Analyse

The one screen with a graphic above its list:

```
 WLAN            Stand 2s alt              78%
  62.4 dB   LAUT       124 bpm    B▮▮▮ M▮▮ H▮
 ┌──────────────────────────────────────────┐
 │        ╱╲    ╱╲╱╲                        │  ← 36 px sparkline, threshold
 │ ──────╱──╲──╱─────────────── thr ─────── │    dashed, gaps hatched
 └──────────────────────────────────────────┘
 > Bereich       ◂ 15m ▸
   Schwelle      ▮▮▮▮▮▯▯▯  65 dB
   Auto          [an]
 ;. waehlen  ,/ aendern
```

Top block (24 px): SPL in the 26-px font, red-inverted while the server's
`warn_over` bit is set (never recomputed locally); BPM shown only above
confidence 0.15; three band bars from `disco.bands` (bass 0–6, mid 7–17,
high 18–23 of the 24 bands, the dashboard's split).

Sparkline (36 px): from `/api/dbhist` (§4.2); the warn threshold as a dashed
line; gaps as hatched columns; a failed fetch keeps the last drawing.

List (scrolls, 3 rows visible):

| Control | Kind | Range | Sends |
|---|---|---|---|
| Bereich | Choice | 1m / 15m / 1h / 24h | local; refetch cadence 5 / 20 / 30 / 60 s |
| Schwelle | Level | 40–110 dB, step 1 | `disco warn_thr` (turns auto off, server-side) |
| Auto | Toggle | | `disco auto_thr {on}` |
| Strip-Warn | Toggle | | `disco warn_hue {on}` |
| Iris | Toggle | | `tf iris {on}` |
| LAUT-Log | Toggle | | `disco quiet_log {on}` |
| Vibe | Level | 0–100, step 10 | `disco vibe` |

## 4. Data flow

### 4.1 Snapshot v2 (additive)

The `/api/dash` snapshot stays the one thing polled. New fields, terse like
the existing ones:

```
hue.g[].c   "#rrggbb"  display colour (gateway converts xy/ct/hue+sat)
hue.pir     bool
lw.c        "#rrggbb"     lw.spd  1-100
yam.bass    -6.0..6.0     yam.treb   yam.xbass bool   yam.sleep 0|30|60|90|120
tf.mx       mode          tf.iris bool
disco.theme  disco.sens 0-100  disco.asens bool  disco.bri 10-100
disco.tgt hue|strip|both   disco.col "#rrggbb"
disco.thr 40-110  disco.over bool  disco.auto bool  disco.vibe 0-100
disco.quiet bool  disco.warn bool   disco.bands [b,m,h] 0-100
fog.est  seconds remaining | null
pi.fan   rpm | null
```

Budget: a full fixture must serialise under **1300 B** (test), against a
device buffer raised from 1600 to **2048 B**. Measured today: 721 B; the
additions are estimated at ~300 B.

### 4.2 On-demand fetches

Three things do not belong in every poll. Each is a GET the device issues
only while the screen that needs it is open, through the same worker and
token, with its own reply kind:

| Endpoint | Returns | Size |
|---|---|---|
| `GET /api/dbhist?range=1m|15m|1h|24h` | `{r, n:120, lo, hi, v:[120 ints 0-255], g:[[i,j]…], thr}` — the gateway fetches disco's 300 points and resamples to 120 columns on absolute time steps (the dashboard's anti-dance rule), maps gaps to column spans | ~450 B |
| `GET /api/scenes?group=81` | `{s:[{i, n}]}`, names folded to 15 chars, at most 12 | ~300 B |
| `GET /api/wx` | `{h:[{t,tp,ic,pop}×4], d:[{d,lo,hi,ic}×5], z:"Zambretti text", rain:"label"}` | ~350 B |

### 4.3 New gateway actions

All named, all whitelisted, all range-checked in `actions.py`; the contract
block in the firmware tests and `test_action_contract.py` grow to cover each.

```
hue:   color{group,preset 0-7}  hue{group,hue,sat}  ct{group,ct}  scene{group,scene}
       pir{on}  mood{name}  all{on}
lw:    speed{speed}  color{r,g,b}
yam:   bass{db}  treble{db}  xbass{on}  sleep{min}  scene{n}
tf:    eq{type,dir}  balance{dir}  matrix{mode}  iris{on}
disco: toggle  theme  sens  autosens{on}  bri  targets  color
       warn_thr  auto_thr{on}  warn_hue{on}  quiet_log{on}  vibe
fog:   refill{full|ml}
```

One existing action changes meaning: `disco toggle` today resolves to
`/api/start` or `/api/stop` (Hue only). The master switch on the device must
hit `POST /api/toggle`, which respects `targets` (Hue / strip / both). The
gateway's `disco on/off` keep their current routes.

The 8 colour presets live in **one** table on the gateway (name, hex, xy,
ct) and are mirrored verbatim in `lib/core/colour.cpp`; a test on each side
pins the values from the dashboard (`hexToXy` output, listed in the research
notes). The device sends a preset *index*; the gateway sends the xy/ct.

## 5. Colour

`lib/core/colour`: `hueToRgb(deg)`, `rgbTo565`, `ctTo565` (warm→cold ramp
for the Weisston bar), the preset table. Swatches draw with the preset's hex;
a room's dot draws with the gateway's `c`. Hue-wheel steps of 15° give 24
stops around the circle — enough resolution for "a bit warmer", few enough
to sweep in a second.

Presets (from the dashboard): warm white ct 366; red, green, blue, yellow,
magenta, cyan, orange as xy (exact values in `colour.cpp`).

## 6. Firmware structure

New in `lib/core` (all pure, all tested):

| Module | Decides |
|---|---|
| `controls` | `Control`, `ControlList`, `buildScreen(screen, dash, ui)` from per-app tables, `adjust()`/`activate()` → `Intent`, cursor/scroll rules, Space target |
| `colour` | conversions and the preset table |
| `sparkline` | `/api/dbhist` payload → columns, threshold row, gap spans, in screen pixels |
| `forecast` | `/api/wx` payload → the readout rows |

`ui_state::handleKey` delegates control screens to `controls`; the
`Screen` enum gains `Room`, `Scenes`, `Forecast`, `Db`. `optimistic` gains
fields for every new adjustable value; `dash` parses snapshot v2.

Shell: `hw_ui` gets one `drawControls(list)` with nine small renderers and
`drawDbHeader`; `hw_net` gets `requestFetch(kind, path)` with a reply kind
so on-demand payloads route to their parser. Nothing else changes shape.

## 7. Testing

- **Core:** every screen's table is valid (steps divide ranges, labels fit
  the row, Choice lists equal the gateway whitelists — extending the existing
  cross-check); every control's adjust/activate emits the intended body (the
  contract block grows from 16 to ~45 literals); colour math against known
  points; sparkline decode incl. gaps and clamping; forecast rows; overlay
  settle for every new field.
- **Gateway:** every new action's validation and plan; `dbhist` resampling
  (300→120, absolute steps, gap mapping, empty input); `scenes` folding;
  snapshot v2 fixture under budget; colour presets equal the firmware table
  (read from `colour.cpp` source — the two-ended pin).
- **Shell pins:** the renderer switch covers every `ControlKind`
  (mutation: drop one → red); fonts declared; on-demand fetches only while
  their screen is open (pin on `loop()`).
- **Docs:** README key table and a per-app control table are generated from
  the core tables by `tools/gen_controls_doc.py` and drift-checked like the
  IR table.
- **Device:** each stage ends with a flashed build and a five-line checklist
  for the owner — the device is now confirmed working, so device-side
  checks are real for the first time.

## 8. Stages

Each stage is flashable on its own and leaves the previous behaviour intact.

1. **Grammar** — `controls` module, renderer, migrate the seven existing
   screens to lists, Space, scroll mark, 13-px Home. No new gateway work;
   the user sees the same functions in the new form.
2. **dB-Analyse** — snapshot v2 disco fields, `/api/dbhist`, the dB screen,
   Home row 8, the seven controls, Iris via `tf`.
3. **Colours** — `colour` module, the shared preset table, room screen with
   Farbe/Farbton/Weisston/Szenen (`/api/scenes`), strip Farbe/Farbton/Tempo,
   disco Farbe, room colour dots.
4. **Extras** — Yamaha EQ/Extra Bass/Sleep/Szenen, Teufel Matrix/Iris/EQ/
   Balance (both paths), Hue PIR/Stimmung/Alle, Nebel Nachfuellen, Vorhersage
   (`/api/wx`), Pi fan.

Each stage gets its own implementation plan (writing-plans), its own preflight
run, its own push. Nothing in a later stage is started before the earlier one
is on the device.

## 9. Open questions resolved here

- *Enter toggles or opens on the room list?* Opens; Space toggles. Enter is
  "go", Space is "power", everywhere.
- *Colour as a wheel picture?* No — a 2-axis picker has no keyboard shape.
  Presets for the common case, a hue Level for the rest.
- *dB history in every poll?* No — 8–12 KB from disco, 450 B after the
  gateway resamples, and only while the screen is open.
- *Teufel EQ level?* Unknowable; a Stepper with no state is the honest form.
