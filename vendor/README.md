# vendor/

Files copied verbatim from elsewhere in the house. They are **inputs**, not
sources to edit here.

## teufel-ir-mapping.csv

The canonical Teufel PowerHiFi IR code table, captured once from the original
remote and used ever since by `powerhifi-controller` (`ir_bridge.py`,
`arduino/teufel-power-hifi-ir-tx.ino`).

Origin: `powerhifi-controller/arduino/teufel-power-hifi-ir-mapping.csv`
Carrier: NEC, 38 kHz, address `0x5780`.

`tools/gen_ir_table.py` turns this into `firmware/lib/core/ir_teufel.h`.
The table is **not** hand-copied into firmware source: a second copy drifts,
and a drifted IR table fails silently — the LED blinks, the box ignores it.

### Known quirk, do not re-debug

`Mute` (0x28) reaches the box and does nothing, while Power and Volume work.
The byte is mislabelled in the original capture; the cause is unknown. This is
documented house-wide — if mute does not work over IR, that is not your bug.
