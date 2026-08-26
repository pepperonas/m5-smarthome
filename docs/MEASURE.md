# Measuring what has not been measured

Everything in this file is **open**. No Cardputer was attached while this was
built, so no device-side number in the documentation is a measurement. Rather
than estimate them, they are listed here with the procedure to obtain them.

Estimated battery life is deliberately absent: it is the product of two
numbers nobody has taken yet.

## 1. Does the keyboard actually wake the ESP32?

Everything else rests on this. The TCA8418 on the ADV scans the key matrix by
itself and raises an interrupt on GPIO 11, which is inside the ESP32-S3 RTC
domain — so in principle it can serve as an `ext0` wake source. In principle.

```bash
cd tools/sleep-probe
pio run -t upload -t monitor
```

Press a key, then leave it alone. After 8 s it prints `SLEEPING` and sleeps.
Press a key again.

Look for:

- `wake cause = 2 (EXT0 — the keyboard woke us: the architecture holds)`
- `ext0 on GPIO 11: accepted`
- `INT line idle level: 1` — idle high, so `esp_sleep_enable_ext0_wakeup(pin, 0)`
  (wake on low) is the right polarity.

**If EXT0 never fires**, the sleeping-remote design does not work as built and
needs a different plan — most plausibly light sleep with the I²C bus alive, or
a timer wake with a much shorter duty cycle. Say so rather than shipping a
device that misses key presses.

## 2. Wake to usable

The probe prints `boot -> first draw` on every EXT0 wake. For the real
firmware, the number that matters is *press to a screen you can act on*, which
includes drawing the cached snapshot but **not** the Wi-Fi association — the
snapshot comes from RTC memory precisely so it does not have to wait.

Add a second stopwatch for *press to fresh data* and record both. Expect the
first to be tens of milliseconds and the second to be dominated by the
association; if the second is over ~1.5 s with a valid AP hint, the fast path
is not working — check that `store::loadApHint` returns true and that
`net::Status::usedFastPath` is set.

## 3. Quiescent current

The board cannot measure its own sleep draw. Either:

- break the battery line into a multimeter set to µA and read it while the
  screen says `SLEEPING`, or
- remove the battery and use an inline USB power meter (coarser, but enough to
  tell 50 µA from 5 mA).

Take three numbers: deep sleep, awake with the screen dim, awake with Wi-Fi
transmitting. With those and the 1750 mAh cell, standby time follows.

Sanity check: if deep sleep reads in the milliamps rather than the microamps,
something is still powered — most likely the display backlight or the radio,
so verify `Display.sleep()` and `WiFi.mode(WIFI_OFF)` both ran before
`esp_deep_sleep_start()`.

## 4. Frame time

`ui::draw` builds an off-screen canvas and blits it whole. Time it across a
few hundred frames on the home screen and on the room list. The budget is one
frame per redraw plus a 1 Hz tick, so this is unlikely to be tight — but
measure rather than assume, because the number ends up in the documentation.

## 5. Infrared

Point the device at the Teufel and press `w` on the Teufel screen to switch
the transport to IR, then volume up.

- Does the amplifier respond at all? If not, check the LEDC carrier is on
  GPIO 44 and that the frame timing survived FreeRTOS preemption — the sender
  busy-waits with `delayMicroseconds`, which is right for 560 µs units but is
  not hard real-time. If it is marginal, pin the send to a high-priority task
  or suspend the scheduler for the ~68 ms frame.
- Note the working range and angle.
- **Mute is expected not to work.** Byte 0x28 reaches the box and does
  nothing, while power and volume work. This is a known, unexplained defect in
  the original capture, documented house-wide. Do not spend time on it.

## 6. Wi-Fi reconnect behaviour

Take the access point down while the device is awake. The header should show
`kein WLAN`, values should stay on screen dimmed with an age, and presses
should be queued rather than lost. Bring it back: the queued presses should go
out and the display should catch up.

Then move the AP to a different channel and wake the device. The stale hint
should fail once, be discarded, and the next attempt should succeed with a
full scan.

## Recording the results

Put the numbers in `README.md` under Status and in
`ARCHITECTURE.md`, replacing the sentences that currently say they were not
measured. Then delete the corresponding section here.

Write the number after taking it, never before — this house has twice had an
estimate land in a commit message ahead of the measurement, and both had to be
amended.
