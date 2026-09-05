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

}  // namespace core
