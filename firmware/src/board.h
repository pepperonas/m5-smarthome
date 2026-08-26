// Board facts, every one of them read out of the vendor library or the
// official example — none guessed. A wrong pin here is a silent failure that
// looks exactly like broken hardware.
#pragma once

#include <cstdint>

namespace board {

// TCA8418 keyboard controller interrupt (Cardputer ADV).
// Source: M5Cardputer/src/utility/Keyboard/KeyboardReader/TCA8418.cpp,
//         DEFAULT_TCA8418_INT_PIN 11.
// This is the pin the whole battery story hangs on: the TCA8418 scans the
// matrix by itself and pulls this line, so the ESP32 can be asleep and still
// notice a key press. GPIO 11 is inside the ESP32-S3 RTC domain (0..21), so
// it can serve as an ext0 wake source.
constexpr int kKeyboardIntPin = 11;

// Infrared LED.
// Source: M5Cardputer/examples/Basic/ir_nec/ir_nec.ino, IR_TX_PIN 44.
// Checked against the speaker pins (41/43/42 on both Cardputer variants) —
// no conflict; the GPIO 44 speaker in M5Unified belongs to the StampPLC.
constexpr int kIrTxPin = 44;

// Battery sense.
// Source: M5Unified/src/utility/Power_Class.cpp — _batAdcPin 10, ratio 2.0.
constexpr int kBatteryAdcPin = 10;
constexpr float kBatteryAdcRatio = 2.0f;

// Display, per the Cardputer datasheet: 240x135 landscape.
constexpr int kScreenW = 240;
constexpr int kScreenH = 135;

}  // namespace board
