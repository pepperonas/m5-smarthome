# Controls

*Generated from `firmware/controls.json` by `tools/gen_controls.py`; do not edit.*

Every app screen is a list of controls. `;` `.` move the cursor, `,` `/` (or `-` `+`) adjust the selected control, `Enter` flips a toggle, opens a link or fires an action, `Space` flips the screen's primary toggle. Readouts are skipped by the cursor. See the design spec for the grammar.

## Home

| Control | Kind | Range | Key |
|---|---|---|---|
| `1 Raeume` | oeffnet |  |  |
| `2 Strip` | oeffnet |  |  |
| `3 Yamaha` | oeffnet |  |  |
| `4 Teufel` | oeffnet |  |  |
| `5 Disco` | oeffnet |  |  |
| `6 Nebel` | oeffnet |  |  |
| `7 Klima` | oeffnet |  |  |

## Rooms

*Built from the snapshot at runtime (one row per room).*

## Room

| Control | Kind | Range | Key |
|---|---|---|---|
| `An` | an/aus |  | `p` |
| `Helligkeit` | Regler | 1 … 254 (als %), Schritt 30 |  |

## Lichtwerk

| Control | Kind | Range | Key |
|---|---|---|---|
| `Strom` | an/aus |  | `p` |
| `Helligkeit` | Regler | 0 … 255 (als %), Schritt 32 |  |
| `Effekt` | Auswahl (sendet sofort) |  | `e` |

## Yamaha

| Control | Kind | Range | Key |
|---|---|---|---|
| `Strom` | an/aus |  | `p` |
| `Pegel` | Regler | -80.0 … -20.0 dB, Schritt 1 dB |  |
| `Eingang` | Auswahl (sendet sofort) |  | `i` |
| `Stumm` | an/aus |  | `m` |

## Teufel

| Control | Kind | Range | Key |
|---|---|---|---|
| `Strom ~` | an/aus |  | `p` |
| `Weg` | Auswahl (sendet sofort) |  | `w` |
| `Lautst. ~` | Regler | 0 … 50, Schritt 1 |  |
| `Eingang ~` | Auswahl (sendet sofort) |  | `i` |
| `Stumm ~` | an/aus |  | `m` |

## Disco

| Control | Kind | Range | Key |
|---|---|---|---|
| `Lichter` | an/aus |  | `p` |
| `Modus` | Auswahl (sendet sofort) |  | `o` |

## Fog

| Control | Kind | Range | Key |
|---|---|---|---|
| `Nebel` | an/aus |  | `p` |
| `Tank` | Anzeige |  |  |
| `220 V, heiss` | Anzeige |  |  |

## Climate

| Control | Kind | Range | Key |
|---|---|---|---|
| `Innen` | Anzeige |  |  |
| `Garten` | Anzeige |  |  |
| `Wetter` | Anzeige |  |  |
| `—` | Anzeige |  |  |
| `Pi` | Anzeige |  |  |
