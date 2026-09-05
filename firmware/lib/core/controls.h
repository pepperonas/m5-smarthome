// Control lists: every app screen as rows the same five keys operate.
//
// The static shape of a screen (which controls, bounds, accelerators) is
// generated into controls_table.h from controls.json. This module fills the
// rows with the live snapshot, moves the cursor, and turns a key on a row
// into the same Intent the shell already knows how to send. Pure.
#pragma once

#include <cstdint>

#include "controls_table.h"
#include "dash.h"
#include "ui_state.h"

namespace core {

constexpr int kMaxControls = 16;
constexpr int kControlTextLen = 20;

struct Control {
    ControlKind kind = ControlKind::Readout;
    Bind bind = Bind::None;
    const char* label = "";
    int value = 0;                    // Level: current; Choice: index; Toggle/Link: on/off
    char text[kControlTextLen] = {0}; // right-hand text (Choice/Readout/Link)
    int min = 0, max = 0, step = 1;
    Fmt fmt = Fmt::Plain;
    int key = 0;                      // room id on room rows
    bool enabled = true;
    char accel = 0;
    uint16_t swatch[8] = {0};         // Color only; filled by a later stage
};

struct ControlList {
    Control items[kMaxControls];
    int count = 0;
    int visibleRows = 7;
};

// ⚠️ Lifetime: `Control::label` is a borrowed pointer. For controls from the
// table it points into static storage; for a room row it points at
// `Room::name` inside the Dash that built the list. A ControlList is
// therefore a short-lived local, valid only as long as the Dash it was built
// from. Never store one across frames, and never build one from a temporary.

// Fill `out` for screen `s` from the snapshot and the UI state.
void buildScreen(Screen s, const Dash& d, const UiState& st, ControlList& out);

// Readouts are shown but never selected.
bool selectable(const Control& c);

// Cursor movement. Readouts are skipped; movement wraps. `from` may be -1.
int firstSelectable(const ControlList& l);
int nextSelectable(const ControlList& l, int from, int dir);

// The scroll window: returns the first visible row so that `cursor` is on
// screen, moving `scroll` as little as possible.
int firstVisible(const ControlList& l, int cursor, int scroll);

// Space flips this one: the first Toggle on the screen, or -1.
int primaryToggle(const ControlList& l);

// Index of the control whose accelerator is `ch`, or -1.
int findAccel(const ControlList& l, char ch);

}  // namespace core
