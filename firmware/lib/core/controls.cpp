#include "controls.h"

#include <cstdio>
#include <cstring>

namespace core {
namespace {

void setText(Control& c, const char* s) {
    snprintf(c.text, sizeof(c.text), "%s", s ? s : "");
}

int indexOf(const char* value, const char* const* list, int count) {
    for (int i = 0; i < count; ++i) {
        if (value && strcmp(value, list[i]) == 0) return i;
    }
    return -1;
}

void setStr(char* dst, int cap, const char* src) {
    int n = 0;
    while (src && src[n] && n < cap - 1) { dst[n] = src[n]; ++n; }
    dst[n] = 0;
}

Intent make(const char* target, const char* action, int arg = 0, bool hasArg = false,
            int arg2 = 0, bool hasArg2 = false) {
    Intent i;
    i.valid = true;
    setStr(i.target, sizeof(i.target), target);
    setStr(i.action, sizeof(i.action), action);
    i.arg = arg; i.hasArg = hasArg; i.arg2 = arg2; i.hasArg2 = hasArg2;
    return i;
}

int cycle(int i, int n, int dir) { return n <= 0 ? 0 : (i + dir + n) % n; }

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

KeyResult refused(const Control& c, UiState& st, uint32_t nowMs) {
    KeyResult out;
    out.redraw = true;
    const bool strip = c.bind == Bind::LwBri || c.bind == Bind::LwEffect || c.bind == Bind::LwOn;
    toast(st, strip ? "Strip-Warn aktiv" : "nicht verfuegbar", nowMs);
    return out;
}

KeyResult roomBrightness(const Dash& d, int roomId, int dir, int step) {
    KeyResult out;
    out.redraw = true;
    const Room* r = findRoom(d, roomId);
    if (!r) return out;
    const int bri = clampi((int)r->bri + dir * step, 1, 254);
    out.intent = make("hue", "bri", roomId, true, bri, true);
    snprintf(out.intent.label, sizeof(out.intent.label), "%s %d%%", r->name, (bri * 100) / 254);
    return out;
}

Control fromSpec(const ControlSpec& s) {
    Control c;
    c.kind = s.kind;
    c.bind = s.bind;
    c.label = s.label;
    c.min = s.min;
    c.max = s.max;
    c.step = s.step;
    c.fmt = s.fmt;
    c.accel = s.key;
    return c;
}

// Values that come from the house. Anything not listed keeps its defaults.
void fill(Control& c, const Dash& d, const UiState& st) {
    char b[kControlTextLen];
    switch (c.bind) {
        case Bind::HomeRooms:
            snprintf(b, sizeof(b), "%d an", d.hue.litCount);
            setText(c, d.valid ? b : "-");
            break;
        case Bind::HomeStrip:
            setText(c, d.lw.warnOwned ? "Warn-Modus" : (d.lw.on ? "an" : "aus"));
            break;
        case Bind::HomeYamaha:
            if (d.sourceOk(SRC_YAM)) {
                snprintf(b, sizeof(b), "%.1f %s", (double)d.yam.db,
                         d.yam.on ? d.yam.input : "aus");
                setText(c, b);
            } else {
                setText(c, "?");
            }
            break;
        case Bind::HomeTeufel:
            snprintf(b, sizeof(b), "%d %s ~", d.tf.volume, d.tf.input);
            setText(c, d.sourceOk(SRC_TF) ? b : "?");
            break;
        case Bind::HomeDisco:
            if (d.disco.on) snprintf(b, sizeof(b), "%d bpm  %.0f dB", d.disco.bpm, (double)d.disco.spl);
            else snprintf(b, sizeof(b), "aus  %.0f dB", (double)d.disco.spl);
            setText(c, b);
            break;
        case Bind::HomeFog:
            if (d.fog.tankPct >= 0) snprintf(b, sizeof(b), "%s  Tank %d%%", d.fog.on ? "AN" : "aus", d.fog.tankPct);
            else snprintf(b, sizeof(b), "%s", d.fog.on ? "AN" : "aus");
            setText(c, b);
            break;
        case Bind::HomeClimate:
            if (d.indoor.valid && d.outdoor.valid)
                snprintf(b, sizeof(b), "%.1f / %.1f C", (double)d.indoor.temp, (double)d.outdoor.temp);
            else if (d.indoor.valid)
                snprintf(b, sizeof(b), "%.1f C", (double)d.indoor.temp);
            else
                snprintf(b, sizeof(b), "?");
            setText(c, b);
            break;

        case Bind::RoomOn:
        case Bind::RoomBri: {
            const Room* r = findRoom(d, st.roomId);
            c.key = st.roomId;
            if (!r) { c.enabled = false; break; }
            c.value = c.bind == Bind::RoomOn ? (r->on ? 1 : 0) : (int)r->bri;
            break;
        }

        case Bind::LwOn:     c.value = d.lw.on ? 1 : 0; break;
        case Bind::LwBri:    c.value = d.lw.bri; c.enabled = !d.lw.warnOwned; break;
        case Bind::LwEffect: {
            const int i = indexOf(d.lw.effect, kLwEffects, kLwEffectCount);
            c.value = i < 0 ? st.lwEffect : i;
            setText(c, i < 0 ? d.lw.effect : kLwEffects[i]);
            c.enabled = !d.lw.warnOwned;
            break;
        }

        case Bind::YamOn:   c.value = d.yam.on ? 1 : 0; break;
        case Bind::YamVol:  c.value = d.yam.raw; break;
        case Bind::YamInput: {
            const int i = indexOf(d.yam.input, kYamahaInputs, kYamahaInputCount);
            c.value = i < 0 ? st.yamInput : i;
            setText(c, d.yam.input);
            break;
        }
        case Bind::YamMute: c.value = d.yam.mute ? 1 : 0; break;

        case Bind::TfPath:
            c.value = st.teufelUseIr ? 1 : 0;
            setText(c, st.teufelUseIr ? "IR (blind)" : "Netz");
            break;
        case Bind::TfOn:    c.value = d.tf.on ? 1 : 0; break;
        case Bind::TfVol:   c.value = d.tf.volume; break;
        case Bind::TfInput: {
            const int i = indexOf(d.tf.input, kTeufelInputs, kTeufelInputCount);
            c.value = i < 0 ? st.tfInput : i;
            setText(c, d.tf.input);
            break;
        }
        case Bind::TfMute:  c.value = d.tf.mute ? 1 : 0; break;

        case Bind::DiscoOn: c.value = d.disco.on ? 1 : 0; break;
        case Bind::DiscoMode: {
            const int i = indexOf(d.disco.mode, kDiscoModes, kDiscoModeCount);
            c.value = i < 0 ? st.discoMode : i;
            setText(c, d.disco.mode);
            break;
        }

        case Bind::FogOn:   c.value = d.fog.on ? 1 : 0; break;
        case Bind::FogTank:
            if (d.fog.tankPct >= 0) snprintf(b, sizeof(b), "%d%% (%d ml)", d.fog.tankPct, d.fog.tankMl);
            else snprintf(b, sizeof(b), "?");
            setText(c, b);
            break;
        case Bind::FogNote: break;

        case Bind::ClimaIn:
            if (d.indoor.valid) snprintf(b, sizeof(b), "%.1f C  %d%%", (double)d.indoor.temp, d.indoor.humidity);
            else snprintf(b, sizeof(b), "?");
            setText(c, b);
            break;
        case Bind::ClimaOut:
            if (d.outdoor.valid) snprintf(b, sizeof(b), "%.1f C  %d%%", (double)d.outdoor.temp, d.outdoor.humidity);
            else snprintf(b, sizeof(b), "?");
            setText(c, b);
            break;
        case Bind::ClimaWx:
            if (d.wx.valid) snprintf(b, sizeof(b), "%.1f C  %d/%d", (double)d.wx.temp, d.wx.high, d.wx.low);
            else snprintf(b, sizeof(b), "?");
            setText(c, b);
            break;
        case Bind::ClimaWxDesc:
            setText(c, d.wx.valid ? d.wx.desc : "");
            break;
        case Bind::ClimaPi:
            if (d.pi.valid) snprintf(b, sizeof(b), "%.0f%% CPU  %.0f C", (double)d.pi.cpu, (double)d.pi.temp);
            else snprintf(b, sizeof(b), "?");
            setText(c, b);
            break;

        case Bind::None:
        default:
            break;
    }
}

}  // namespace

bool selectable(const Control& c) { return c.kind != ControlKind::Readout; }

void buildScreen(Screen s, const Dash& d, const UiState& st, ControlList& out) {
    out.count = 0;
    out.visibleRows = s == Screen::Home ? 8 : 7;

    if (s == Screen::Rooms) {
        // One Link per room, built from the snapshot; the right-hand text is
        // what the old room list showed.
        for (int i = 0; i < d.hue.count && out.count < kMaxControls; ++i) {
            const Room& r = d.hue.rooms[i];
            Control c;
            c.kind = ControlKind::Link;
            c.bind = Bind::None;
            c.label = r.name;
            c.key = r.id;
            c.value = r.on ? 1 : 0;
            if (r.on) snprintf(c.text, sizeof(c.text), "%d%%", (r.bri * 100) / 254);
            else snprintf(c.text, sizeof(c.text), "aus");
            out.items[out.count++] = c;
        }
        return;
    }

    int n = 0;
    const ControlSpec* specs = specsFor(s, n);
    for (int i = 0; i < n && out.count < kMaxControls; ++i) {
        Control c = fromSpec(specs[i]);
        fill(c, d, st);
        out.items[out.count++] = c;
    }
}

int firstSelectable(const ControlList& l) {
    for (int i = 0; i < l.count; ++i) if (selectable(l.items[i])) return i;
    return -1;
}

int nextSelectable(const ControlList& l, int from, int dir) {
    if (l.count == 0) return -1;
    int i = from;
    for (int n = 0; n < l.count; ++n) {
        i = (i + dir + l.count) % l.count;
        if (selectable(l.items[i])) return i;
    }
    return from;                       // nothing else to land on
}

int firstVisible(const ControlList& l, int cursor, int scroll) {
    const int rows = l.visibleRows > 0 ? l.visibleRows : 1;
    if (scroll < 0) scroll = 0;
    if (cursor < scroll) scroll = cursor;
    if (cursor >= scroll + rows) scroll = cursor - rows + 1;
    const int maxScroll = l.count > rows ? l.count - rows : 0;
    if (scroll > maxScroll) scroll = maxScroll;
    return scroll < 0 ? 0 : scroll;
}

int primaryToggle(const ControlList& l) {
    for (int i = 0; i < l.count; ++i)
        if (l.items[i].kind == ControlKind::Toggle) return i;
    return -1;
}

int findAccel(const ControlList& l, char ch) {
    if (!ch) return -1;
    for (int i = 0; i < l.count; ++i) if (l.items[i].accel == ch) return i;
    return -1;
}

KeyResult adjust(const ControlList& l, int idx, int dir, const Dash& d,
                 UiState& st, uint32_t nowMs) {
    KeyResult out;
    out.redraw = true;
    if (idx < 0 || idx >= l.count) return out;
    const Control& c = l.items[idx];
    if (!c.enabled) return refused(c, st, nowMs);
    out.viaIr = st.teufelUseIr && (c.bind == Bind::TfVol || c.bind == Bind::TfInput);

    switch (c.kind) {
        case ControlKind::Toggle:
            return activate(l, idx, d, st, nowMs);

        case ControlKind::Link:
            // Room rows: brightness straight from the list, the fast path.
            if (c.key) return roomBrightness(d, c.key, dir, 30);
            return out;

        case ControlKind::Level: {
            const int next = clampi(c.value + dir * c.step, c.min, c.max);
            switch (c.bind) {
                case Bind::RoomBri:
                    return roomBrightness(d, c.key, dir, c.step);
                case Bind::LwBri:
                    out.intent = make("lw", "bri", 0, false, next, true);
                    snprintf(out.intent.label, sizeof(out.intent.label), "Strip %d%%", (next * 100) / 255);
                    return out;
                case Bind::YamVol:
                    if (next == c.value) return out;           // at the edge: nothing past it
                    out.intent = make("yam", "vol", dir * 2, true);
                    snprintf(out.intent.label, sizeof(out.intent.label), "%.1f dB", next / 10.0);
                    return out;
                case Bind::TfVol:
                    if (next == c.value) return out;
                    out.intent = make("tf", "vol", dir, true);
                    setStr(out.intent.label, sizeof(out.intent.label), dir > 0 ? "Teufel lauter" : "Teufel leiser");
                    return out;
                default:
                    return out;
            }
        }

        case ControlKind::Choice: {
            switch (c.bind) {
                case Bind::TfPath:
                    st.teufelUseIr = !st.teufelUseIr;
                    toast(st, st.teufelUseIr ? "Weg: IR (blind)" : "Weg: Netz", nowMs);
                    return out;
                case Bind::LwEffect: {
                    st.lwEffect = cycle(c.value, kLwEffectCount, dir);
                    out.intent = make("lw", "effect");
                    setStr(out.intent.name, sizeof(out.intent.name), kLwEffects[st.lwEffect]);
                    snprintf(out.intent.label, sizeof(out.intent.label), "Effekt %s", kLwEffects[st.lwEffect]);
                    return out;
                }
                case Bind::YamInput: {
                    st.yamInput = cycle(c.value, kYamahaInputCount, dir);
                    out.intent = make("yam", "input");
                    setStr(out.intent.name, sizeof(out.intent.name), kYamahaInputs[st.yamInput]);
                    snprintf(out.intent.label, sizeof(out.intent.label), "Eingang %s", kYamahaInputs[st.yamInput]);
                    return out;
                }
                case Bind::TfInput: {
                    st.tfInput = cycle(c.value, kTeufelInputCount, dir);
                    out.intent = make("tf", "input");
                    setStr(out.intent.name, sizeof(out.intent.name), kTeufelInputs[st.tfInput]);
                    snprintf(out.intent.label, sizeof(out.intent.label), "Eingang %s", kTeufelInputs[st.tfInput]);
                    return out;
                }
                case Bind::DiscoMode: {
                    st.discoMode = cycle(c.value, kDiscoModeCount, dir);
                    out.intent = make("disco", "mode");
                    setStr(out.intent.name, sizeof(out.intent.name), kDiscoModes[st.discoMode]);
                    snprintf(out.intent.label, sizeof(out.intent.label), "Modus %s", kDiscoModes[st.discoMode]);
                    return out;
                }
                default:
                    return out;
            }
        }

        case ControlKind::Picker:
        case ControlKind::Stepper:
        case ControlKind::Color:
        case ControlKind::Action:
        case ControlKind::Readout:
            return out;                 // later stages give these meaning
    }
    return out;
}

KeyResult activate(const ControlList& l, int idx, const Dash& d, UiState& st,
                   uint32_t nowMs) {
    KeyResult out;
    out.redraw = true;
    if (idx < 0 || idx >= l.count) return out;
    const Control& c = l.items[idx];
    if (!c.enabled) return refused(c, st, nowMs);
    out.viaIr = st.teufelUseIr && (c.bind == Bind::TfOn || c.bind == Bind::TfMute);
    const bool on = c.value != 0;

    switch (c.kind) {
        case ControlKind::Toggle:
            switch (c.bind) {
                case Bind::RoomOn:
                    return toggleRoom(d, c.key, st, nowMs);
                case Bind::LwOn:
                    out.intent = make("lw", on ? "off" : "on");
                    setStr(out.intent.label, sizeof(out.intent.label), on ? "Strip aus" : "Strip an");
                    return out;
                case Bind::YamOn:
                    out.intent = make("yam", on ? "off" : "on");
                    setStr(out.intent.label, sizeof(out.intent.label), on ? "Yamaha aus" : "Yamaha an");
                    return out;
                case Bind::YamMute:
                    out.intent = make("yam", "mute");
                    setStr(out.intent.label, sizeof(out.intent.label), on ? "Ton an" : "Stumm");
                    return out;
                case Bind::TfOn:
                    out.intent = make("tf", "power");
                    setStr(out.intent.label, sizeof(out.intent.label), "Teufel Power");
                    return out;
                case Bind::TfMute:
                    out.intent = make("tf", "mute");
                    // Documented house quirk: this byte reaches the box and
                    // does nothing. Say so rather than let the user think it
                    // is broken.
                    toast(st, "Mute: bekannt wirkungslos", nowMs);
                    return out;
                case Bind::DiscoOn:
                    out.intent = make("disco", on ? "off" : "on");
                    setStr(out.intent.label, sizeof(out.intent.label), on ? "Disco aus" : "Disco an");
                    return out;
                case Bind::FogOn:
                    if (on) {
                        // Switching a heater off is never gated.
                        out.intent = make("fog", "off");
                        setStr(out.intent.label, sizeof(out.intent.label), "Nebel aus");
                        return out;
                    } else {
                        Intent i = make("fog", "on");
                        i.needsConfirm = true;
                        setStr(i.label, sizeof(i.label), "Nebel AN");
                        st.confirming = true;
                        st.pending = i;
                        return out;                  // nothing sent yet
                    }
                default:
                    return out;
            }

        case ControlKind::Link:
            switch (c.bind) {
                case Bind::HomeRooms:   st.screen = Screen::Rooms; break;
                case Bind::HomeStrip:   st.screen = Screen::Lichtwerk; break;
                case Bind::HomeYamaha:  st.screen = Screen::Yamaha; break;
                case Bind::HomeTeufel:  st.screen = Screen::Teufel; break;
                case Bind::HomeDisco:   st.screen = Screen::Disco; break;
                case Bind::HomeFog:     st.screen = Screen::Fog; break;
                case Bind::HomeClimate: st.screen = Screen::Climate; break;
                default:
                    if (c.key) { st.roomId = c.key; st.screen = Screen::Room; }
                    break;
            }
            st.cursor = 0;
            st.scroll = 0;
            return out;

        case ControlKind::Level:
        case ControlKind::Choice:
        case ControlKind::Picker:
        case ControlKind::Stepper:
        case ControlKind::Color:
        case ControlKind::Action:
        case ControlKind::Readout:
            return out;
    }
    return out;
}

KeyResult toggleRoom(const Dash& d, int roomId, UiState& st, uint32_t nowMs) {
    (void)st; (void)nowMs;
    KeyResult out;
    out.redraw = true;
    const Room* r = findRoom(d, roomId);
    if (!r) return out;
    out.intent = make("hue", r->on ? "off" : "on", roomId, true);
    snprintf(out.intent.label, sizeof(out.intent.label), "%s %s", r->name, r->on ? "aus" : "an");
    return out;
}

Screen parentScreen(Screen s) {
    return s == Screen::Room ? Screen::Rooms : Screen::Home;
}

}  // namespace core
