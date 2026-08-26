// Screen and key handling — the whole interaction model, with no pixels in it.
//
// handleKey() is a pure function: (state, key, dash) -> (new state, intent).
// Drawing and the keyboard driver sit outside and stay thin. This is what
// makes the button layout something you can assert about on a laptop.
#pragma once

#include <cstdint>

#include "command.h"
#include "dash.h"

namespace core {

enum class Screen : uint8_t {
    Home, Rooms, Lichtwerk, Yamaha, Teufel, Disco, Fog, Climate, Console,
};

// A key, already normalised by the keyboard adapter.
// The Cardputer has no dedicated arrow keys; the arrows are printed on
// ; . , /  and that is what the adapter maps here.
struct Key {
    char ch = 0;          // printable character, 0 if none
    bool up = false;      // ';'
    bool down = false;    // '.'
    bool left = false;    // ','
    bool right = false;   // '/'  (on Home this opens the console instead)
    bool enter = false;
    bool esc = false;     // backtick / esc
    bool del = false;     // backspace
    bool tab = false;
};

// What the UI wants done. `intent.valid` false means "nothing to send".
struct UiState {
    Screen screen = Screen::Home;
    int cursor = 0;               // selected row on list screens

    // Console
    char input[40] = {0};
    int inputLen = 0;

    // Confirmation gate. Anything that can start the fog machine parks here
    // first; nothing is sent until the user says yes explicitly.
    bool confirming = false;
    Intent pending;

    // Transport for the Teufel: the network path is authoritative, IR is the
    // fallback that also works while the Pi reboots.
    bool teufelUseIr = false;

    // 'i' / 'e' / 'o' step through these. Kept as indices rather than names
    // so a press is one increment and needs no string handling.
    int yamInput = 0;
    int tfInput = 0;
    int lwEffect = 0;
    int discoMode = 0;

    char toast[40] = {0};         // transient message under the header
    uint32_t toastUntilMs = 0;
};

struct KeyResult {
    Intent intent;                // what to send, if anything
    bool redraw = false;
    bool viaIr = false;           // send by infrared rather than the gateway
};

// Handle one key. Mutates `st`, returns what should be sent.
KeyResult handleKey(UiState& st, const Key& k, const Dash& d, uint32_t nowMs);

// Set the transient message line.
void toast(UiState& st, const char* msg, uint32_t nowMs);
bool toastVisible(const UiState& st, uint32_t nowMs);

// Number of rows on the current screen (for cursor clamping and drawing).
int rowCount(const UiState& st, const Dash& d);

constexpr uint32_t kToastMs = 2500;

// Cycle lists. Short on purpose: a remote offers the inputs you actually use,
// not every one the device enumerates.
extern const char* const kYamahaInputs[];
extern const int kYamahaInputCount;
extern const char* const kTeufelInputs[];
extern const int kTeufelInputCount;
extern const char* const kLwEffects[];
extern const int kLwEffectCount;
extern const char* const kDiscoModes[];
extern const int kDiscoModeCount;

}  // namespace core
