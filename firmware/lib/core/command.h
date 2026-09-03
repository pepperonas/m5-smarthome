// The typed command line.
//
// A Cardputer has 56 real keys. On a device like this, typing "wohnzimmer aus"
// beats walking a menu tree every time — so the command line is a first-class
// input path, not a gimmick, and its matching lives here where it can be tested.
//
// Pure: takes the text and the current Dash (for room names), returns an
// intent. It never performs anything.
#pragma once

#include <cstddef>

#include <cstdint>

#include "dash.h"

namespace core {

struct Intent {
    bool valid = false;
    char target[8] = {0};       // "hue", "lw", "yam", "tf", "fog", "disco", "macro"
    char action[10] = {0};      // "on", "off", "toggle", "vol", "bri", ...
    int arg = 0;                // group id, step, brightness ...
    bool hasArg = false;
    int arg2 = 0;               // second number where an action needs one
    bool hasArg2 = false;
    // Anything that ignites the fog machine must be confirmed by the user
    // *before* it is sent. The command line is exactly where a typo happens.
    bool needsConfirm = false;
    // A named value the action needs: an input, an effect, a mode. Separate
    // from `hint`, which is for telling the user why something did not work.
    char name[16] = {0};
    char label[28] = {0};       // what to echo back: "Wohnzimmer aus"
    char hint[32] = {0};        // set when invalid: why, or the nearest match
};

// Parse one typed line. `d` supplies live room names so "kueche an" works
// without hard-coding the house.
Intent parseCommand(const char* input, const Dash& d);

// Serialise an intent into the JSON the gateway's /api/act expects. Returns
// false — and an empty string — for an invalid intent or one that would not
// fit: a truncated object is a 400 at best and a different request at worst.
// The key names are the contract with gateway/m5gw/actions.py; the test
// block ACTION_BODY_CONTRACT pins them from both sides.
bool buildActionBody(const Intent& in, char* out, size_t cap);

// Suggestion for the current input, or nullptr. Used for inline completion.
const char* completeCommand(const char* input, const Dash& d, char* buf,
                            int bufLen);

// --- exposed for testing -------------------------------------------------

// Case-insensitive, umlaut-folding comparison score of `needle` against
// `hay`: 100 exact, 90 prefix, 70 substring, 65 one typo, 55 two typos,
// 0 no match. Words shorter than four letters are never fuzzy-matched.
int matchScore(const char* needle, const char* hay);

// Fold a word for comparison: lower-case, ae/oe/ue/ss, strip non-alphanumerics.
void foldWord(const char* in, char* out, int outLen);

}  // namespace core
