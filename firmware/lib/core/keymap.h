// Translating a keyboard report into a Key.
//
// Pure on purpose. The bug that created this file lived in the "thin adapter"
// layer that was declared too trivial to test: the vendor's isChange() is
// *consuming* — it updates its own last-seen state and returns false on the
// second call — and the adapter called it twice per loop, so every key press
// was detected and then immediately read back as empty. No key did anything.
//
// The consuming call now happens exactly once and its result is passed in
// here, where the mapping can be checked on a host.
#pragma once

#include <cstddef>

#include "ui_state.h"

namespace core {

//: What the vendor library reports for one keyboard scan.
struct KeyReport {
    bool changed = false;     // isChange(), read exactly once by the caller
    int pressedCount = 0;     // isPressed()
    bool enter = false;
    bool del = false;
    bool tab = false;
    bool fn = false;
    const char* word = nullptr;   // printable characters, may be empty
    size_t wordLen = 0;
};

// True when this report should be handled at all.
bool hasKeyEvent(const KeyReport& r);

// Map a report onto a Key. Returns false when there is nothing to handle,
// in which case `out` is left untouched.
//
// The Cardputer has no dedicated arrow keys: the arrows are printed on
// ; . , / and the library reports those keys as exactly those characters,
// with no Fn involved.
bool mapKey(const KeyReport& r, Key& out);

}  // namespace core
