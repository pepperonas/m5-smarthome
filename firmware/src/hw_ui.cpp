#include "hw_ui.h"

#include <M5Cardputer.h>

#include <cstdio>
#include <cstring>

#include "board.h"

namespace ui {
namespace {

// A dark palette: this thing gets picked up in a dim living room.
constexpr uint16_t kBg      = 0x0000;
constexpr uint16_t kFg      = 0xFFFF;
constexpr uint16_t kDim     = 0x8410;   // stale / secondary
constexpr uint16_t kAccent  = 0x5D9F;   // house accent blue
constexpr uint16_t kOn      = 0xFE60;   // warm amber = something is lit
constexpr uint16_t kWarn    = 0xF800;
constexpr uint16_t kHeadBg  = 0x18E3;

constexpr int kHeaderH = 14;
constexpr int kFooterH = 13;
constexpr int kRowH = 14;
constexpr int kContentY = kHeaderH + 2;

M5Canvas* g_canvas = nullptr;

void text(int x, int y, const char* s, uint16_t colour) {
    g_canvas->setTextColor(colour, kBg);
    g_canvas->drawString(s, x, y);
}

void drawHeader(const core::Dash& d, uint32_t nowMs, const net::Status& link,
                int batteryPct, bool unconfirmed) {
    g_canvas->fillRect(0, 0, board::kScreenW, kHeaderH, kHeadBg);
    g_canvas->setTextColor(kFg, kHeadBg);

    const char* linkStr = "---";
    uint16_t linkCol = kDim;
    switch (link.link) {
        case net::LinkState::Online:
            linkStr = "WLAN"; linkCol = kAccent; break;
        case net::LinkState::Connecting:
            linkStr = "..."; linkCol = kDim; break;
        case net::LinkState::Failed:
            linkStr = "kein WLAN"; linkCol = kWarn; break;
        default: break;
    }
    g_canvas->setTextColor(linkCol, kHeadBg);
    g_canvas->drawString(linkStr, 3, 3);

    // The honesty line. A snapshot that could not be refreshed is still shown
    // — dimmed and labelled — because blanking it would make every Wi-Fi
    // hiccup look like a dead house.
    char mid[28];
    if (!d.valid) {
        snprintf(mid, sizeof(mid), "warte auf Daten");
    } else if (core::isStale(d, nowMs)) {
        snprintf(mid, sizeof(mid), "Stand %lus alt",
                 (unsigned long)(core::ageMs(d, nowMs) / 1000));
    } else if (unconfirmed) {
        snprintf(mid, sizeof(mid), "IR unbestaetigt");
    } else {
        mid[0] = 0;
    }
    if (mid[0]) {
        g_canvas->setTextColor(core::isStale(d, nowMs) ? kDim : kOn, kHeadBg);
        g_canvas->drawCenterString(mid, board::kScreenW / 2, 3);
    }

    char bat[8];
    snprintf(bat, sizeof(bat), "%d%%", batteryPct);
    g_canvas->setTextColor(batteryPct <= 15 ? kWarn : kFg, kHeadBg);
    g_canvas->drawRightString(bat, board::kScreenW - 3, 3);
}

void drawFooter(const core::UiState& st, uint32_t nowMs, const char* hint) {
    const int y = board::kScreenH - kFooterH;
    g_canvas->fillRect(0, y, board::kScreenW, kFooterH, kHeadBg);
    if (core::toastVisible(st, nowMs)) {
        g_canvas->setTextColor(kOn, kHeadBg);
        g_canvas->drawString(st.toast, 3, y + 2);
    } else if (hint) {
        g_canvas->setTextColor(kDim, kHeadBg);
        g_canvas->drawString(hint, 3, y + 2);
    }
}

void row(int index, const char* left, const char* right, bool selected,
         bool lit, bool stale) {
    const int y = kContentY + index * kRowH;
    if (selected) {
        g_canvas->fillRect(0, y - 1, board::kScreenW, kRowH, 0x2124);
        // A 2 px bar plus a caret: on a 240x135 panel a tint alone is easy to
        // miss, and a cursor you cannot find reads as a frozen device.
        g_canvas->fillRect(0, y - 1, 2, kRowH, kAccent);
        g_canvas->setTextColor(kAccent, 0x2124);
        g_canvas->drawString(">", 4, y);
    }
    const uint16_t col = stale ? kDim : (lit ? kOn : kFg);
    g_canvas->setTextColor(col, selected ? 0x2124 : kBg);
    g_canvas->drawString(left, selected ? 12 : 6, y);
    if (right) {
        g_canvas->setTextColor(stale ? kDim : kFg, selected ? 0x2124 : kBg);
        g_canvas->drawRightString(right, board::kScreenW - 5, y);
    }
}

void drawHome(const core::UiState& st, const core::Dash& d,
              uint32_t nowMs) {
    char buf[32];
    const bool anyData = d.valid;

    snprintf(buf, sizeof(buf), "%d an", d.hue.litCount);
    row(0, "1 Raeume", anyData ? buf : "-", st.cursor == 0, d.hue.litCount > 0,
        !d.sourceOk(core::SRC_HUE) || d.sourceStale(core::SRC_HUE));

    row(1, "2 Strip", d.lw.warnOwned ? "Warn-Modus" : (d.lw.on ? "an" : "aus"),
        st.cursor == 1, d.lw.on, d.sourceStale(core::SRC_LW));

    if (d.sourceOk(core::SRC_YAM)) {
        snprintf(buf, sizeof(buf), "%.1f %s", (double)d.yam.db,
                 d.yam.on ? d.yam.input : "aus");
    } else {
        snprintf(buf, sizeof(buf), "?");
    }
    row(2, "3 Yamaha", buf, st.cursor == 2, d.yam.on,
        d.sourceStale(core::SRC_YAM));

    // The Teufel is always an estimate — the Pi flips a flag after firing IR
    // and nothing ever confirms it. The tilde says so on every frame.
    snprintf(buf, sizeof(buf), "%d %s ~", d.tf.volume, d.tf.input);
    row(3, "4 Teufel", d.sourceOk(core::SRC_TF) ? buf : "?", st.cursor == 3,
        d.tf.on, d.sourceStale(core::SRC_TF));

    if (d.disco.on) {
        snprintf(buf, sizeof(buf), "%d bpm  %.0f dB", d.disco.bpm,
                 (double)d.disco.spl);
    } else {
        snprintf(buf, sizeof(buf), "aus  %.0f dB", (double)d.disco.spl);
    }
    row(4, "5 Disco", buf, st.cursor == 4, d.disco.on,
        d.sourceStale(core::SRC_DISCO));

    if (d.fog.tankPct >= 0) {
        snprintf(buf, sizeof(buf), "%s  Tank %d%%", d.fog.on ? "AN" : "aus",
                 d.fog.tankPct);
    } else {
        snprintf(buf, sizeof(buf), "%s", d.fog.on ? "AN" : "aus");
    }
    row(5, "6 Nebel", buf, st.cursor == 5, d.fog.on,
        d.sourceStale(core::SRC_FOG));

    if (d.indoor.valid && d.outdoor.valid) {
        snprintf(buf, sizeof(buf), "%.1f / %.1f C", (double)d.indoor.temp,
                 (double)d.outdoor.temp);
    } else if (d.indoor.valid) {
        snprintf(buf, sizeof(buf), "%.1f C", (double)d.indoor.temp);
    } else {
        snprintf(buf, sizeof(buf), "?");
    }
    row(6, "7 Klima", buf, st.cursor == 6, false,
        d.sourceStale(core::SRC_CLIMA) || d.indoor.ageSeconds > 0);
}

void drawRooms(const core::UiState& st, const core::Dash& d) {
    if (d.hue.count == 0) {
        text(6, kContentY, "keine Raeume bekannt", kDim);
        return;
    }
    // Scroll so the cursor stays visible on a 135 px screen.
    constexpr int kVisible = 7;
    int first = st.cursor - kVisible / 2;
    if (first < 0) first = 0;
    if (first > d.hue.count - kVisible) first = d.hue.count - kVisible;
    if (first < 0) first = 0;

    for (int i = 0; i < kVisible && first + i < d.hue.count; ++i) {
        const core::Room& r = d.hue.rooms[first + i];
        char right[16];
        if (r.on) {
            snprintf(right, sizeof(right), "%d%%", (r.bri * 100) / 254);
        } else {
            snprintf(right, sizeof(right), "aus");
        }
        row(i, r.name, right, first + i == st.cursor, r.on,
            d.sourceStale(core::SRC_HUE));
    }
}

void drawDetail(const core::UiState& st, const core::Dash& d) {
    char buf[40];
    switch (st.screen) {
        case core::Screen::Lichtwerk:
            text(6, kContentY, "Lichtwerk", kAccent);
            snprintf(buf, sizeof(buf), "Strom:      %s", d.lw.on ? "an" : "aus");
            text(6, kContentY + kRowH, buf, d.lw.on ? kOn : kFg);
            snprintf(buf, sizeof(buf), "Helligkeit: %d%%", (d.lw.bri * 100) / 255);
            text(6, kContentY + kRowH * 2, buf, kFg);
            snprintf(buf, sizeof(buf), "Effekt:     %s", d.lw.effect);
            text(6, kContentY + kRowH * 3, buf, kFg);
            text(6, kContentY + kRowH * 5, "e Effekt  +/- Helligkeit", kDim);
            if (d.lw.warnOwned) {
                text(6, kContentY + kRowH * 4,
                     "Strip-Warn aktiv (Disco)", kWarn);
            }
            break;

        case core::Screen::Yamaha:
            text(6, kContentY, "Yamaha RX-V577", kAccent);
            snprintf(buf, sizeof(buf), "Strom:      %s", d.yam.on ? "an" : "Standby");
            text(6, kContentY + kRowH, buf, d.yam.on ? kOn : kFg);
            snprintf(buf, sizeof(buf), "Pegel:      %.1f dB", (double)d.yam.db);
            text(6, kContentY + kRowH * 2, buf, kFg);
            snprintf(buf, sizeof(buf), "Eingang:    %s", d.yam.input);
            text(6, kContentY + kRowH * 3, buf, kFg);
            if (d.yam.mute) text(6, kContentY + kRowH * 4, "STUMM", kWarn);
            text(6, kContentY + kRowH * 5, "i Eingang  m Stumm  +/- Pegel", kDim);
            break;

        case core::Screen::Teufel:
            text(6, kContentY, "Teufel PowerHiFi", kAccent);
            snprintf(buf, sizeof(buf), "Strom:      %s (geschaetzt)",
                     d.tf.on ? "an" : "aus");
            text(6, kContentY + kRowH, buf, d.tf.on ? kOn : kFg);
            snprintf(buf, sizeof(buf), "Lautstaerke:%d", d.tf.volume);
            text(6, kContentY + kRowH * 2, buf, kFg);
            snprintf(buf, sizeof(buf), "Eingang:    %s", d.tf.input);
            text(6, kContentY + kRowH * 3, buf, kFg);
            snprintf(buf, sizeof(buf), "Weg:        %s",
                     st.teufelUseIr ? "IR (blind)" : "Netz");
            text(6, kContentY + kRowH * 4, buf,
                 st.teufelUseIr ? kWarn : kAccent);
            text(6, kContentY + kRowH * 5, "w Weg  i Eingang  +/- Pegel", kDim);
            break;

        case core::Screen::Disco:
            text(6, kContentY, "Disco", kAccent);
            snprintf(buf, sizeof(buf), "Lichter:    %s", d.disco.on ? "an" : "aus");
            text(6, kContentY + kRowH, buf, d.disco.on ? kOn : kFg);
            snprintf(buf, sizeof(buf), "Tempo:      %d bpm", d.disco.bpm);
            text(6, kContentY + kRowH * 2, buf, kFg);
            snprintf(buf, sizeof(buf), "Pegel:      %.1f dB", (double)d.disco.spl);
            text(6, kContentY + kRowH * 3, buf, kFg);
            snprintf(buf, sizeof(buf), "Modus:      %s", d.disco.mode);
            text(6, kContentY + kRowH * 4, buf, kFg);
            text(6, kContentY + kRowH * 5, "o Modus  Enter schaltet", kDim);
            break;

        case core::Screen::Fog:
            text(6, kContentY, "Nebelmaschine", kAccent);
            snprintf(buf, sizeof(buf), "Zustand:    %s", d.fog.on ? "AN" : "aus");
            text(6, kContentY + kRowH, buf, d.fog.on ? kWarn : kFg);
            if (d.fog.tankPct >= 0) {
                snprintf(buf, sizeof(buf), "Tank:       %d%% (%d ml)",
                         d.fog.tankPct, d.fog.tankMl);
                text(6, kContentY + kRowH * 2, buf,
                     d.fog.tankPct < 20 ? kWarn : kFg);
            }
            text(6, kContentY + kRowH * 4, "220 V und heiss.", kWarn);
            text(6, kContentY + kRowH * 5, "Enter fragt erst nach.", kDim);
            break;

        case core::Screen::Climate: {
            text(6, kContentY, "Klima und Wetter", kAccent);
            if (d.indoor.valid) {
                snprintf(buf, sizeof(buf), "Innen:      %.1f C  %d%%",
                         (double)d.indoor.temp, d.indoor.humidity);
                text(6, kContentY + kRowH, buf,
                     d.indoor.ageSeconds > 0 ? kDim : kFg);
            }
            if (d.outdoor.valid) {
                snprintf(buf, sizeof(buf), "Garten:     %.1f C  %d%%",
                         (double)d.outdoor.temp, d.outdoor.humidity);
                text(6, kContentY + kRowH * 2, buf,
                     d.outdoor.ageSeconds > 0 ? kDim : kFg);
            }
            if (d.wx.valid) {
                snprintf(buf, sizeof(buf), "Wetter:     %.1f C  %d/%d",
                         (double)d.wx.temp, d.wx.high, d.wx.low);
                text(6, kContentY + kRowH * 3, buf, kFg);
                text(6, kContentY + kRowH * 4, d.wx.desc, kDim);
            }
            if (d.pi.valid) {
                snprintf(buf, sizeof(buf), "Pi:         %.0f%% CPU  %.0f C",
                         (double)d.pi.cpu, (double)d.pi.temp);
                text(6, kContentY + kRowH * 5, buf, kDim);
            }
            break;
        }

        default:
            break;
    }
}

void drawDiagnostics(const core::Dash& d, uint32_t nowMs,
                     const net::Status& link) {
    char buf[52];
    text(6, kContentY, "Diagnose", kAccent);

    const char* linkName = link.link == net::LinkState::Online ? "online"
                         : link.link == net::LinkState::Connecting ? "verbindet"
                         : link.link == net::LinkState::Failed ? "fehlgeschlagen"
                         : "aus";
    snprintf(buf, sizeof(buf), "WLAN %s  %s  %d dBm", linkName, link.ip,
             link.rssi);
    text(6, kContentY + kRowH, buf,
         link.link == net::LinkState::Online ? kFg : kWarn);

    text(6, kContentY + kRowH * 2, link.url[0] ? link.url : "(noch kein Abruf)",
         kDim);

    // The single most useful line: what the gateway actually answered.
    if (link.lastError[0]) {
        snprintf(buf, sizeof(buf), "Fehler: %s", link.lastError);
        text(6, kContentY + kRowH * 3, buf, kWarn);
    } else if (link.lastStatus) {
        snprintf(buf, sizeof(buf), "HTTP %d, %u B", link.lastStatus,
                 (unsigned)link.lastBytes);
        text(6, kContentY + kRowH * 3, buf, kOn);
    } else {
        text(6, kContentY + kRowH * 3, "noch keine Antwort", kDim);
    }

    snprintf(buf, sizeof(buf), "Abrufe %lu, davon Fehler %lu",
             (unsigned long)link.requests, (unsigned long)link.failed);
    text(6, kContentY + kRowH * 4, buf, kFg);

    if (d.valid) {
        snprintf(buf, sizeof(buf), "Snapshot: %lus alt, %d Raeume",
                 (unsigned long)(core::ageMs(d, nowMs) / 1000), d.hue.count);
        text(6, kContentY + kRowH * 5, buf, kFg);
    } else {
        text(6, kContentY + kRowH * 5, "Snapshot: NIE geparst", kWarn);
    }

    snprintf(buf, sizeof(buf), "Heap %lu B  Stack frei %lu W",
             (unsigned long)link.freeHeap, (unsigned long)link.stackHighWater);
    text(6, kContentY + kRowH * 6, buf, kDim);
}

void drawConsole(const core::UiState& st) {
    text(6, kContentY, "Befehl:", kAccent);
    char line[48];
    snprintf(line, sizeof(line), "> %s_", st.input);
    text(6, kContentY + kRowH, line, kFg);
    text(6, kContentY + kRowH * 3, "z.B. wohnzimmer aus", kDim);
    text(6, kContentY + kRowH * 4, "     flur 50   nebel", kDim);
    text(6, kContentY + kRowH * 5, "Tab ergaenzt, Esc zurueck", kDim);
}

void drawConfirm(const core::UiState& st) {
    g_canvas->fillRect(10, 35, board::kScreenW - 20, 62, 0x3000);
    g_canvas->drawRect(10, 35, board::kScreenW - 20, 62, kWarn);
    g_canvas->setTextColor(kWarn, 0x3000);
    g_canvas->drawCenterString("Wirklich?", board::kScreenW / 2, 42);
    g_canvas->setTextColor(kFg, 0x3000);
    g_canvas->drawCenterString(st.pending.label, board::kScreenW / 2, 60);
    g_canvas->setTextColor(kDim, 0x3000);
    g_canvas->drawCenterString("Enter/j = ja, sonst nein",
                               board::kScreenW / 2, 78);
}

}  // namespace

void begin() {
    // One off-screen canvas, blitted whole: partial updates on this panel
    // tear visibly when a row changes under the cursor.
    g_canvas = new M5Canvas(&M5Cardputer.Display);
    g_canvas->createSprite(board::kScreenW, board::kScreenH);
    g_canvas->setTextSize(1);
    g_canvas->setTextFont(&fonts::Font0);
}

void setBacklight(uint8_t level) { M5Cardputer.Display.setBrightness(level); }

void draw(const core::UiState& st, const core::Dash& d, uint32_t nowMs,
          const net::Status& link, int batteryPct, bool unconfirmed) {
    if (!g_canvas) return;
    g_canvas->fillSprite(kBg);

    drawHeader(d, nowMs, link, batteryPct, unconfirmed);

    const char* hint = ";. waehlen  Enter oeffnen  d Diagnose";
    switch (st.screen) {
        case core::Screen::Home:    drawHome(st, d, nowMs); break;
        case core::Screen::Rooms:
            drawRooms(st, d);
            hint = "Enter schaltet  +/- Helligkeit";
            break;
        case core::Screen::Console: drawConsole(st); hint = nullptr; break;
        case core::Screen::Diagnostics:
            drawDiagnostics(d, nowMs, link);
            hint = "Esc zurueck";
            break;
        default:
            drawDetail(st, d);
            hint = "Enter schaltet  Esc zurueck";
            break;
    }

    drawFooter(st, nowMs, hint);
    if (st.confirming) drawConfirm(st);

    g_canvas->pushSprite(0, 0);
}

void drawSetup(const char* prompt, const char* value, bool masked, int step) {
    if (!g_canvas) return;
    g_canvas->fillSprite(kBg);
    g_canvas->setTextColor(kAccent, kBg);
    g_canvas->drawString("Einrichtung", 6, 6);
    char stepStr[16];
    snprintf(stepStr, sizeof(stepStr), "%d/4", step + 1);
    g_canvas->drawRightString(stepStr, board::kScreenW - 6, 6);

    g_canvas->setTextColor(kFg, kBg);
    g_canvas->drawString(prompt, 6, 32);

    char shown[48];
    if (masked) {
        size_t n = strnlen(value, sizeof(shown) - 2);
        for (size_t i = 0; i < n; ++i) shown[i] = '*';
        shown[n] = 0;
    } else {
        snprintf(shown, sizeof(shown), "%s", value);
    }
    char line[52];
    snprintf(line, sizeof(line), "> %s_", shown);
    g_canvas->drawString(line, 6, 52);

    g_canvas->setTextColor(kDim, kBg);
    g_canvas->drawString("Enter = weiter", 6, 92);
    g_canvas->drawString("Nichts davon liegt im Build.", 6, 106);
    g_canvas->pushSprite(0, 0);
}

}  // namespace ui
