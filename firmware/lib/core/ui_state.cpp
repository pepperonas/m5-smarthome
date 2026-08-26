#include "ui_state.h"

#include <cstdio>
#include <cstring>

namespace core {
namespace {

void setStr(char* dst, int cap, const char* src) {
    int n = 0;
    while (src && src[n] && n < cap - 1) { dst[n] = src[n]; ++n; }
    dst[n] = 0;
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

// Digit shortcuts on the home screen. One key press, one destination — the
// point of having 56 keys is not having to walk a tree.
Screen screenForDigit(char c) {
    switch (c) {
        case '1': return Screen::Rooms;
        case '2': return Screen::Lichtwerk;
        case '3': return Screen::Yamaha;
        case '4': return Screen::Teufel;
        case '5': return Screen::Disco;
        case '6': return Screen::Fog;
        case '7': return Screen::Climate;
        default:  return Screen::Home;
    }
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
        case Screen::Home:  return 7;
        default:            return 0;
    }
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
