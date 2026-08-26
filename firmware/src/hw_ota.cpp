#include "hw_ota.h"

#include <ArduinoOTA.h>
#include <M5Cardputer.h>
#include <WiFi.h>

#include <cstdio>

#include "board.h"

namespace ota {
namespace {

void line(int y, const char* s, uint16_t colour) {
    M5Cardputer.Display.setTextColor(colour, TFT_BLACK);
    M5Cardputer.Display.drawString(s, 6, y);
}

void screen(const char* status, int percent) {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    line(6, "Update ueber WLAN", 0x5D9F);

    char buf[48];
    if (WiFi.status() == WL_CONNECTED) {
        snprintf(buf, sizeof(buf), "%s", WiFi.localIP().toString().c_str());
        line(28, buf, TFT_WHITE);
        line(44, "pio run -t upload --upload-port", 0x8410);
        line(58, "  <diese Adresse>", 0x8410);
    } else {
        line(28, "kein WLAN", 0xF800);
    }

    line(84, status, TFT_WHITE);
    if (percent >= 0) {
        const int w = (board::kScreenW - 24) * percent / 100;
        M5Cardputer.Display.drawRect(12, 100, board::kScreenW - 24, 10, 0x8410);
        M5Cardputer.Display.fillRect(12, 100, w, 10, 0x5D9F);
    } else {
        line(100, "Esc bricht ab", 0x8410);
    }
}

}  // namespace

void runMode() {
    if (WiFi.status() != WL_CONNECTED) {
        screen("WLAN noetig", -1);
        delay(2000);
        return;
    }

    ArduinoOTA.setHostname("m5-smarthome");
    // No password set here on purpose: adding one would mean either compiling
    // a secret in — which this project does not do anywhere — or inventing a
    // second stored credential for a mode that only exists while somebody is
    // holding the device and looking at it.
    ArduinoOTA.onStart([]() { screen("Empfange...", 0); });
    ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
        static int last = -1;
        const int pct = total ? (int)(done * 100 / total) : 0;
        if (pct != last) {          // redrawing every packet would crawl
            last = pct;
            screen("Empfange...", pct);
        }
    });
    ArduinoOTA.onEnd([]() { screen("Fertig, Neustart", 100); });
    ArduinoOTA.onError([](ota_error_t) { screen("Fehlgeschlagen", -1); });
    ArduinoOTA.begin();

    screen("Warte auf Upload", -1);

    for (;;) {
        ArduinoOTA.handle();
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            const auto st = M5Cardputer.Keyboard.keysState();
            for (auto c : st.word) {
                if (c == '`') return;      // Esc leaves the mode
            }
        }
        delay(5);
    }
}

}  // namespace ota
