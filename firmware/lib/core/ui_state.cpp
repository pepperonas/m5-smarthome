#include "ui_state.h"

#include <cstdio>
#include <cstring>

namespace core {

// Verified against the receiver's own Input_Sel_Item list and the gateway
// whitelists; anything not on these lists is refused server-side anyway.
const char* const kYamahaInputs[] = {"Spotify", "AirPlay", "HDMI1", "HDMI2",
                                     "HDMI3", "AV1", "TUNER", "AUX"};
const int kYamahaInputCount = 8;

const char* const kTeufelInputs[] = {"AUX", "LINE", "OPTICAL", "USB",
                                     "BLUETOOTH"};
const int kTeufelInputCount = 5;

// Mirrors valid_effects in lichtwerk-controller, minus iris_warn, which
// belongs to the disco strip-warn path.
const char* const kLwEffects[] = {"solid", "rainbow", "pulse", "chase",
                                  "sparkle", "strobe", "meteor", "breathe",
                                  "sinelon", "juggle", "theater", "gradient",
                                  "fire"};
const int kLwEffectCount = 13;

const char* const kDiscoModes[] = {"rainbow", "party", "random", "pulse",
                                   "solid", "strobe"};
const int kDiscoModeCount = 6;

namespace {

void setStr(char* dst, int cap, const char* src) {
    int n = 0;
    while (src && src[n] && n < cap - 1) { dst[n] = src[n]; ++n; }
    dst[n] = 0;
}

// Step an index forward and wrap. `dir` is +1 or -1.
int cycle(int idx, int count, int dir) {
    if (count <= 0) return 0;
    return (idx + dir + count) % count;
}

// Cycle from what the house currently shows, not from a private index that
// starts at 0 and drifts the moment the phone app changes something: on
// HDMI2, 'i' goes to HDMI3, not back to AirPlay. A current value the remote
// does not offer (NET RADIO) keeps the last index, which is still a valid
// entry.
int cycleFrom(const char* current, const char* const* list, int count,
              int lastIdx, int dir) {
    int idx = lastIdx;
    for (int i = 0; i < count; ++i) {
        if (current && strcmp(current, list[i]) == 0) { idx = i; break; }
    }
    return cycle(idx, count, dir);
}

Intent make(const char* target, const char* action, int arg = 0,
            bool hasArg = false, int arg2 = 0, bool hasArg2 = false) {
    Intent i;
    i.valid = true;
    setStr(i.target, sizeof(i.target), target);
    setStr(i.action, sizeof(i.action), action);
    i.arg = arg;
    i.hasArg = hasArg;
    i.arg2 = arg2;
    i.hasArg2 = hasArg2;
    return i;
}

// The home screen in order. The digit shortcuts and the cursor both read
// from this, so they can never disagree about what row 3 is.
const Screen kHomeRows[] = {
    Screen::Rooms, Screen::Lichtwerk, Screen::Yamaha, Screen::Teufel,
    Screen::Disco, Screen::Fog, Screen::Climate,
};
constexpr int kHomeRowCount = 7;

// One key press, one destination — the point of having 56 keys is not having
// to walk a tree. Arrows plus Enter do the same thing for anyone who reaches
// for those first, which most people do.
Screen screenForDigit(char c) {
    const int idx = c - '1';
    return (idx >= 0 && idx < kHomeRowCount) ? kHomeRows[idx] : Screen::Home;
}

}  // namespace

void toast(UiState& st, const char* msg, uint32_t nowMs) {
    setStr(st.toast, sizeof(st.toast), msg);
    st.toastUntilMs = nowMs + kToastMs;
}

bool toastVisible(const UiState& st, uint32_t nowMs) {
    return st.toast[0] != 0 && (int32_t)(st.toastUntilMs - nowMs) > 0;
}

int rowCount(const UiState& st, const Dash& d) {
    switch (st.screen) {
        case Screen::Rooms: return d.hue.count;
        case Screen::Home:  return kHomeRowCount;
        default:            return 0;
    }
}

Screen homeScreenAt(int row) {
    if (row < 0 || row >= kHomeRowCount) return Screen::Home;
    return kHomeRows[row];
}

KeyResult handleKey(UiState& st, const Key& k, const Dash& d, uint32_t nowMs) {
    KeyResult out;
    out.redraw = true;

    // --- confirmation gate ------------------------------------------------
    // Sits in front of everything: while a dangerous action is pending, no
    // other key does anything. y/j/Enter confirms, everything else cancels.
    if (st.confirming) {
        const bool yes = k.enter || k.ch == 'y' || k.ch == 'j';
        if (yes) {
            out.intent = st.pending;
            out.intent.needsConfirm = false;   // the user just did confirm
            toast(st, out.intent.label, nowMs);
        } else {
            toast(st, "abgebrochen", nowMs);
        }
        st.confirming = false;
        st.pending = Intent();
        return out;
    }

    // --- console ----------------------------------------------------------
    if (st.screen == Screen::Console) {
        if (k.esc) {
            st.screen = Screen::Home;
            st.input[0] = 0;
            st.inputLen = 0;
            return out;
        }
        if (k.del) {
            if (st.inputLen > 0) st.input[--st.inputLen] = 0;
            return out;
        }
        if (k.tab) {
            char buf[24];
            if (completeCommand(st.input, d, buf, sizeof(buf))) {
                setStr(st.input, sizeof(st.input), buf);
                st.inputLen = (int)strlen(st.input);
            }
            return out;
        }
        if (k.enter) {
            Intent i = parseCommand(st.input, d);
            st.input[0] = 0;
            st.inputLen = 0;
            if (!i.valid) {
                toast(st, i.hint[0] ? i.hint : "unbekannt", nowMs);
                return out;
            }
            st.screen = Screen::Home;
            if (i.needsConfirm) {
                st.confirming = true;
                st.pending = i;
                return out;                    // nothing sent yet
            }
            out.intent = i;
            toast(st, i.label, nowMs);
            return out;
        }
        if (k.ch >= 32 && k.ch < 127 && st.inputLen < (int)sizeof(st.input) - 1) {
            st.input[st.inputLen++] = k.ch;
            st.input[st.inputLen] = 0;
        }
        return out;
    }

    // --- global keys ------------------------------------------------------
    if (k.esc || k.del) {
        if (st.screen != Screen::Home) {
            st.screen = Screen::Home;
            st.cursor = 0;
        }
        return out;
    }
    if (k.tab) {
        st.screen = Screen::Console;
        st.input[0] = 0;
        st.inputLen = 0;
        return out;
    }

    if (st.screen == Screen::Home) {
        // On Home there is no left/right to navigate, so '/' is free for the
        // console — which is where the briefing wanted it.
        if (k.right || k.ch == '/') {
            st.screen = Screen::Console;
            st.input[0] = 0;
            st.inputLen = 0;
            return out;
        }
        if (k.ch >= '1' && k.ch <= '7') {
            st.screen = screenForDigit(k.ch);
            st.cursor = 0;
            return out;
        }
        if (k.ch == 'g') {                       // gute nacht
            Intent i = make("macro", "goodnight");
            setStr(i.label, sizeof(i.label), "Gute Nacht");
            // A macro that darkens the whole flat is worth one keypress of
            // confirmation, even though it cannot hurt anything.
            st.confirming = true;
            st.pending = i;
            return out;
        }
        // A device with no cable and no console must be able to explain
        // itself, or the only debugging tool left is asking the owner what
        // the screen says.
        if (k.ch == 'd') {
            st.screen = Screen::Diagnostics;
            return out;
        }
        if (k.ch == 'a') {
            out.intent = make("macro", "alloff");
            setStr(out.intent.label, sizeof(out.intent.label), "Alles aus");
            toast(st, "Alles aus", nowMs);
            return out;
        }
    }

    // --- list navigation --------------------------------------------------
    const int rows = rowCount(st, d);
    if (rows > 0) {
        if (k.up) {
            st.cursor = (st.cursor - 1 + rows) % rows;
            return out;
        }
        if (k.down) {
            st.cursor = (st.cursor + 1) % rows;
            return out;
        }
    }

    switch (st.screen) {
        case Screen::Home: {
            // Without this, arrows moved an invisible cursor and Enter did
            // nothing at all — the screen looked frozen to anyone who did not
            // already know about the digit shortcuts.
            if (k.enter || k.right) {
                st.screen = homeScreenAt(st.cursor);
                st.cursor = 0;
                return out;
            }
            break;
        }

        case Screen::Rooms: {
            if (st.cursor < 0 || st.cursor >= d.hue.count) return out;
            const Room& r = d.hue.rooms[st.cursor];
            if (k.enter) {
                out.intent = make("hue", r.on ? "off" : "on", r.id, true);
                snprintf(out.intent.label, sizeof(out.intent.label), "%s %s",
                         r.name, r.on ? "aus" : "an");
                return out;
            }
            if (k.ch == '+' || k.ch == '-' || k.right || k.left) {
                const int delta = (k.ch == '+' || k.right) ? 32 : -32;
                int bri = (int)r.bri + delta;
                if (bri < 1) bri = 1;
                if (bri > 254) bri = 254;
                out.intent = make("hue", "bri", r.id, true, bri, true);
                snprintf(out.intent.label, sizeof(out.intent.label), "%s %d%%",
                         r.name, (bri * 100) / 254);
                return out;
            }
            break;
        }

        case Screen::Lichtwerk: {
            if (k.enter) {
                out.intent = make("lw", d.lw.on ? "off" : "on");
                setStr(out.intent.label, sizeof(out.intent.label),
                       d.lw.on ? "Strip aus" : "Strip an");
                return out;
            }
            if (k.ch == 'e' || k.right || k.left) {
                // The strip belongs to strip-warn while disco drives it;
                // painting over that would fight the audio engine.
                if (d.lw.warnOwned) {
                    toast(st, "Strip-Warn aktiv", nowMs);
                    return out;
                }
                st.lwEffect = cycleFrom(d.lw.effect, kLwEffects, kLwEffectCount,
                                        st.lwEffect, k.left ? -1 : 1);
                out.intent = make("lw", "effect");
                setStr(out.intent.name, sizeof(out.intent.name),
                       kLwEffects[st.lwEffect]);
                snprintf(out.intent.label, sizeof(out.intent.label), "Effekt %s",
                         kLwEffects[st.lwEffect]);
                return out;
            }
            if (k.ch == '+' || k.ch == '-') {
                int bri = (int)d.lw.bri + (k.ch == '+' ? 32 : -32);
                if (bri < 0) bri = 0;
                if (bri > 255) bri = 255;
                out.intent = make("lw", "bri", 0, false, bri, true);
                snprintf(out.intent.label, sizeof(out.intent.label),
                         "Strip %d%%", (bri * 100) / 255);
                return out;
            }
            break;
        }

        case Screen::Yamaha: {
            if (k.enter || k.ch == 'p') {
                out.intent = make("yam", d.yam.on ? "off" : "on");
                setStr(out.intent.label, sizeof(out.intent.label),
                       d.yam.on ? "Yamaha aus" : "Yamaha an");
                return out;
            }
            if (k.ch == '+' || k.ch == '-' || k.right || k.left) {
                const int step = (k.ch == '+' || k.right) ? 2 : -2;
                out.intent = make("yam", "vol", step, true);
                snprintf(out.intent.label, sizeof(out.intent.label), "%.1f dB",
                         (double)(d.yam.raw + step * 5) / 10.0);
                return out;
            }
            if (k.ch == 'i') {
                st.yamInput = cycleFrom(d.yam.input, kYamahaInputs,
                                        kYamahaInputCount, st.yamInput, 1);
                out.intent = make("yam", "input");
                setStr(out.intent.name, sizeof(out.intent.name),
                       kYamahaInputs[st.yamInput]);
                snprintf(out.intent.label, sizeof(out.intent.label),
                         "Eingang %s", kYamahaInputs[st.yamInput]);
                return out;
            }
            if (k.ch == 'm') {
                out.intent = make("yam", "mute");
                setStr(out.intent.label, sizeof(out.intent.label),
                       d.yam.mute ? "Ton an" : "Stumm");
                return out;
            }
            break;
        }

        case Screen::Teufel: {
            // 'w' flips between the gateway and the IR LED. IR works while the
            // Pi reboots and needs no Wi-Fi, but nothing ever confirms it.
            if (k.ch == 'w') {
                st.teufelUseIr = !st.teufelUseIr;
                toast(st, st.teufelUseIr ? "Weg: IR (blind)" : "Weg: Netz", nowMs);
                return out;
            }
            out.viaIr = st.teufelUseIr;
            if (k.enter || k.ch == 'p') {
                out.intent = make("tf", "power");
                setStr(out.intent.label, sizeof(out.intent.label), "Teufel Power");
                return out;
            }
            if (k.ch == '+' || k.ch == '-' || k.right || k.left) {
                const int step = (k.ch == '+' || k.right) ? 1 : -1;
                out.intent = make("tf", "vol", step, true);
                setStr(out.intent.label, sizeof(out.intent.label),
                       step > 0 ? "Teufel lauter" : "Teufel leiser");
                return out;
            }
            if (k.ch == 'i') {
                st.tfInput = cycleFrom(d.tf.input, kTeufelInputs,
                                       kTeufelInputCount, st.tfInput, 1);
                out.intent = make("tf", "input");
                setStr(out.intent.name, sizeof(out.intent.name),
                       kTeufelInputs[st.tfInput]);
                snprintf(out.intent.label, sizeof(out.intent.label),
                         "Eingang %s", kTeufelInputs[st.tfInput]);
                return out;
            }
            if (k.ch == 'm') {
                out.intent = make("tf", "mute");
                // Documented house quirk: this byte reaches the box and does
                // nothing. Say so rather than let the user think it is broken.
                toast(st, "Mute: bekannt wirkungslos", nowMs);
                return out;
            }
            break;
        }

        case Screen::Disco: {
            if (k.ch == 'o' || k.right || k.left) {
                st.discoMode = cycleFrom(d.disco.mode, kDiscoModes,
                                         kDiscoModeCount, st.discoMode,
                                         k.left ? -1 : 1);
                out.intent = make("disco", "mode");
                setStr(out.intent.name, sizeof(out.intent.name),
                       kDiscoModes[st.discoMode]);
                snprintf(out.intent.label, sizeof(out.intent.label), "Modus %s",
                         kDiscoModes[st.discoMode]);
                return out;
            }
            if (k.enter) {
                out.intent = make("disco", d.disco.on ? "off" : "on");
                setStr(out.intent.label, sizeof(out.intent.label),
                       d.disco.on ? "Disco aus" : "Disco an");
                return out;
            }
            break;
        }

        case Screen::Fog: {
            if (k.enter) {
                if (d.fog.on) {
                    // Switching a heater off is never gated.
                    out.intent = make("fog", "off");
                    setStr(out.intent.label, sizeof(out.intent.label), "Nebel aus");
                    return out;
                }
                Intent i = make("fog", "on");
                i.needsConfirm = true;
                setStr(i.label, sizeof(i.label), "Nebel AN");
                st.confirming = true;
                st.pending = i;
                return out;                     // nothing sent yet
            }
            break;
        }

        default:
            break;
    }

    out.redraw = false;
    return out;
}

}  // namespace core
