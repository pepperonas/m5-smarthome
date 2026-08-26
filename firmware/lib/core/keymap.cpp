#include "keymap.h"

namespace core {

bool hasKeyEvent(const KeyReport& r) {
    return r.changed && r.pressedCount > 0;
}

bool mapKey(const KeyReport& r, Key& out) {
    if (!hasKeyEvent(r)) return false;

    Key k;
    k.enter = r.enter;
    k.del = r.del;
    k.tab = r.tab;

    // One character per event: holding two keys at once is not a gesture this
    // UI has, and taking the first keeps repeat behaviour predictable.
    if (r.word && r.wordLen > 0) {
        const char c = r.word[0];
        switch (c) {
            case ';': k.up = true; break;
            case '.': k.down = true; break;
            case ',': k.left = true; break;
            case '/': k.right = true; k.ch = '/'; break;
            case '`': k.esc = true; break;
            default:  k.ch = c; break;
        }
    }

    out = k;
    return true;
}

}  // namespace core
