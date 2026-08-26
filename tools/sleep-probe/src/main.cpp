// Sleep / wake probe for the M5Cardputer ADV.
//
// Answers three questions, in order of how much depends on them:
//
//   1. Does the TCA8418 keyboard controller wake the ESP32 from deep sleep
//      via its interrupt line (GPIO 11)? If not, the "sleeping remote"
//      architecture does not work and needs a different plan.
//   2. How long from wake to a drawn, usable screen?
//   3. What is the quiescent current? (Read on an external meter — the
//      board cannot measure its own sleep draw.)
//
// Usage: flash, open the serial monitor, press any key. The device prints a
// report, sleeps after 8 s, and prints wake timings on the next press.
//
//   pio run -t upload -t monitor
//
// For the current measurement, break the battery line into a multimeter (or
// use a USB power meter with the battery removed) and read the value while
// the banner says SLEEPING.

#include <Arduino.h>
#include <M5Cardputer.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

// Source: M5Cardputer/src/utility/Keyboard/KeyboardReader/TCA8418.cpp
#define KEY_INT_PIN 11

RTC_DATA_ATTR uint32_t g_wakeCount = 0;
RTC_DATA_ATTR uint32_t g_sleepStartedMs = 0;

static uint32_t t_boot = 0;
static uint32_t t_drawn = 0;

void report(const char* what, uint32_t ms) {
    Serial.printf("  %-28s %6lu ms\n", what, (unsigned long)ms);
}

void setup() {
    t_boot = millis();
    Serial.begin(115200);

    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setBrightness(160);
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5Cardputer.Display.setTextSize(1);
    t_drawn = millis();

    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    ++g_wakeCount;

    delay(600);                        // give the USB CDC time to enumerate
    Serial.println();
    Serial.println("=== Cardputer sleep/wake probe ===");
    Serial.printf("boot #%lu, wake cause = %d ", (unsigned long)g_wakeCount,
                  (int)cause);
    switch (cause) {
        case ESP_SLEEP_WAKEUP_EXT0:
            Serial.println("(EXT0 — the keyboard woke us: the architecture holds)");
            break;
        case ESP_SLEEP_WAKEUP_UNDEFINED:
            Serial.println("(power-on / reset, not a wake)");
            break;
        default:
            Serial.println("(something else — investigate)");
            break;
    }

    // Is the pin usable as an RTC wake source at all? esp_sleep returns an
    // error for a pin outside the RTC domain, which is the honest test.
    const esp_err_t rc =
        esp_sleep_enable_ext0_wakeup((gpio_num_t)KEY_INT_PIN, 0);
    Serial.printf("ext0 on GPIO %d: %s\n", KEY_INT_PIN,
                  rc == ESP_OK ? "accepted" : esp_err_to_name(rc));

    pinMode(KEY_INT_PIN, INPUT_PULLUP);
    Serial.printf("INT line idle level: %d (expect 1 = idle high, "
                  "active low on a press)\n", digitalRead(KEY_INT_PIN));

    if (cause == ESP_SLEEP_WAKEUP_EXT0) {
        Serial.println("wake timings:");
        report("boot -> first draw", t_drawn - t_boot);
        report("boot -> this line", millis() - t_boot);
    }

    M5Cardputer.Display.drawString("sleep probe", 6, 6);
    M5Cardputer.Display.drawString("press a key; sleeps in 8 s", 6, 24);
    char b[40];
    snprintf(b, sizeof(b), "boot #%lu  cause %d", (unsigned long)g_wakeCount,
             (int)cause);
    M5Cardputer.Display.drawString(b, 6, 42);
    if (cause == ESP_SLEEP_WAKEUP_EXT0) {
        snprintf(b, sizeof(b), "wake->draw %lu ms",
                 (unsigned long)(t_drawn - t_boot));
        M5Cardputer.Display.drawString(b, 6, 60);
    }
}

void loop() {
    static uint32_t lastKey = millis();
    M5Cardputer.update();

    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        lastKey = millis();
        Serial.printf("key at %lu ms, INT=%d\n", (unsigned long)lastKey,
                      digitalRead(KEY_INT_PIN));
    }

    if (millis() - lastKey > 8000) {
        Serial.println("SLEEPING — measure the quiescent current now.");
        Serial.flush();
        M5Cardputer.Display.fillScreen(TFT_BLACK);
        M5Cardputer.Display.drawString("SLEEPING", 6, 6);
        M5Cardputer.Display.drawString("press a key to wake", 6, 24);
        delay(1200);
        M5Cardputer.Display.setBrightness(0);
        M5Cardputer.Display.sleep();

        rtc_gpio_pullup_en((gpio_num_t)KEY_INT_PIN);
        esp_sleep_enable_ext0_wakeup((gpio_num_t)KEY_INT_PIN, 0);
        g_sleepStartedMs = millis();
        esp_deep_sleep_start();
    }
    delay(10);
}
