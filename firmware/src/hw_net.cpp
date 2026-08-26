#include "hw_net.h"

#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <cstring>

#include "netplan.h"

namespace net {
namespace {

constexpr int kMaxBody = 1600;      // measured snapshot is ~721 B
constexpr int kQueueLen = 6;

struct Job {
    bool isDash;
    uint32_t overlayToken;
    char body[192];
};

struct Reply {
    bool isDash;
    bool ok;
    uint32_t overlayToken;
    int status;
    uint16_t len;
    char body[kMaxBody];
};

store::Config g_cfg;
QueueHandle_t g_jobs = nullptr;
QueueHandle_t g_replies = nullptr;
TaskHandle_t g_task = nullptr;
volatile bool g_busy = false;
bool g_rediscovered = false;
Status g_status;
Reply g_lastReply;

// Connect using the remembered BSSID and channel when we have one. Skipping
// the scan is the difference between "a remote" and "a thing you wait for".
bool connectWifi() {
    core::ApHint hint;
    const bool haveHint = store::loadApHint(hint) &&
                          core::apHintUsable(hint, 0);

    const uint32_t t0 = millis();
    WiFi.mode(WIFI_STA);
    // Modem sleep is wrong for this device. It parks the radio between
    // beacons, which adds up to ~100 ms of latency per exchange — and this
    // thing is only awake for a few seconds at a time before deep sleep,
    // where the radio is off entirely. Trading responsiveness for power we
    // are not spending anyway is a bad deal, and the added latency made
    // short HTTP timeouts fire.
    WiFi.setSleep(false);
    WiFi.persistent(false);

    if (haveHint) {
        if (hint.ip && hint.gw && hint.mask) {
            // Reusing the last lease skips the DHCP round trip as well.
            // The DNS argument is not optional in practice: WiFi.config()
            // without it leaves the resolver empty, and anything that later
            // uses a hostname — mDNS fallback, a moved gateway — fails in a
            // way that looks like the network is down.
            WiFi.config(IPAddress(hint.ip), IPAddress(hint.gw),
                        IPAddress(hint.mask), IPAddress(hint.gw));
        }
        WiFi.begin(g_cfg.ssid, g_cfg.pass, hint.channel, hint.bssid, true);
    } else {
        WiFi.begin(g_cfg.ssid, g_cfg.pass);
    }

    const uint32_t budget = haveHint ? 4000 : 12000;
    while (WiFi.status() != WL_CONNECTED && (millis() - t0) < budget) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (WiFi.status() != WL_CONNECTED) {
        if (haveHint) {
            // The hint was wrong (the AP moved, or we roamed). Drop it and let
            // the next attempt do a full scan rather than failing forever.
            store::clearApHint();
            WiFi.config(IPAddress((uint32_t)0), IPAddress((uint32_t)0),
                        IPAddress((uint32_t)0));
        }
        g_status.link = LinkState::Failed;
        ++g_status.failures;
        return false;
    }

    core::ApHint fresh;
    memcpy(fresh.bssid, WiFi.BSSID(), 6);
    fresh.channel = static_cast<uint8_t>(WiFi.channel());
    fresh.ip = (uint32_t)WiFi.localIP();
    fresh.gw = (uint32_t)WiFi.gatewayIP();
    fresh.mask = (uint32_t)WiFi.subnetMask();
    fresh.savedAtEpoch = 0;           // no wall clock yet; ttl check tolerates it
    fresh.valid = true;
    store::saveApHint(fresh);

    g_status.link = LinkState::Online;
    g_status.rssi = WiFi.RSSI();
    snprintf(g_status.ip, sizeof(g_status.ip), "%s",
             WiFi.localIP().toString().c_str());
    g_status.connectMs = millis() - t0;
    g_status.usedFastPath = haveHint;
    g_status.failures = 0;
    return true;
}

// Ask the LAN where the gateway is. The Pi announces _m5gw._tcp through
// avahi (see gateway/m5-gateway.avahi.service), so a fresh device needs a
// token and a Wi-Fi password, not an IP address somebody has to look up.
//
// Only tried when we have no host, or when the one we have stopped
// answering — a working address is never second-guessed.
bool discoverGateway() {
    if (!MDNS.begin("cardputer")) return false;
    const int n = MDNS.queryService("m5gw", "tcp");
    if (n <= 0) return false;
    const IPAddress ip = MDNS.IP(0);
    snprintf(g_cfg.host, sizeof(g_cfg.host), "%u.%u.%u.%u", ip[0], ip[1],
             ip[2], ip[3]);
    g_cfg.port = MDNS.port(0);
    store::Config saved;
    if (store::load(saved)) {
        strncpy(saved.host, g_cfg.host, sizeof(saved.host) - 1);
        saved.port = g_cfg.port;
        store::save(saved);          // remember it, so next boot skips this
    }
    return true;
}

void buildUrl(char* out, size_t cap, const char* path) {
    snprintf(out, cap, "http://%s:%u%s", g_cfg.host, (unsigned)g_cfg.port, path);
}

void runJob(const Job& job) {
    // Static, not on the stack: this struct is ~1.6 KB and the worker stack
    // is a few KB shared with the whole HTTP path. Only one job runs at a
    // time, so a single instance is safe.
    static Reply r;
    r.isDash = job.isDash;
    r.overlayToken = job.overlayToken;
    r.ok = false;
    r.status = 0;
    r.len = 0;
    r.body[0] = 0;

    if (WiFi.status() != WL_CONNECTED && !connectWifi()) {
        xQueueOverwrite(g_replies, &r);
        return;
    }
    if (g_cfg.host[0] == 0 && !discoverGateway()) {
        xQueueOverwrite(g_replies, &r);
        return;
    }

    char url[96];
    buildUrl(url, sizeof(url), job.isDash ? "/api/dash" : "/api/act");

    snprintf(g_status.url, sizeof(g_status.url), "%s", url);
    ++g_status.requests;

    // An explicit WiFiClient rather than the URL-only overload: that one is
    // deprecated and manages its own client, which interacts badly with
    // setReuse across differing requests.
    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(2500);
    // Generous on purpose. The gateway answers a cold snapshot in ~0.3 s over
    // Ethernet, but this hop is Wi-Fi from a sleeping radio, and a timeout
    // that fires early looks exactly like a dead backend.
    http.setTimeout(job.isDash ? 6000 : 8000);
    if (!http.begin(client, url)) {
        ++g_status.failed;
        snprintf(g_status.lastError, sizeof(g_status.lastError), "begin() failed");
        xQueueOverwrite(g_replies, &r);
        return;
    }
    char auth[80];
    snprintf(auth, sizeof(auth), "Bearer %s", g_cfg.token);
    http.addHeader("Authorization", auth);

    int code;
    if (job.isDash) {
        code = http.GET();
    } else {
        http.addHeader("Content-Type", "application/json");
        code = http.POST((uint8_t*)job.body, strlen(job.body));
    }
    r.status = code;
    g_status.lastStatus = code;
    if (code <= 0) {
        ++g_status.failed;
        snprintf(g_status.lastError, sizeof(g_status.lastError), "HTTP %d %s",
                 code, HTTPClient::errorToString(code).c_str());
    } else if (code < 200 || code >= 300) {
        ++g_status.failed;
        snprintf(g_status.lastError, sizeof(g_status.lastError),
                 "HTTP %d", code);
    } else {
        g_status.lastError[0] = 0;
    }
    if (code > 0) {
        String payload = http.getString();
        const size_t n = payload.length() < (size_t)kMaxBody - 1
                             ? payload.length() : (size_t)kMaxBody - 1;
        memcpy(r.body, payload.c_str(), n);
        r.body[n] = 0;
        r.len = static_cast<uint16_t>(n);
        r.ok = (code >= 200 && code < 300);
    }
    g_status.lastBytes = r.len;
    g_status.freeHeap = ESP.getFreeHeap();
    g_status.stackHighWater = uxTaskGetStackHighWaterMark(nullptr);
    http.end();

    // A transport failure (not an HTTP error) can mean the gateway moved.
    // Re-discover once and retry, rather than staying broken until someone
    // re-runs setup.
    if (code <= 0 && !g_rediscovered) {
        g_rediscovered = true;
        if (discoverGateway()) {
            xQueueSend(g_jobs, &job, 0);      // one retry at the new address
            return;
        }
    }
    if (code > 0) g_rediscovered = false;

    xQueueOverwrite(g_replies, &r);
}

void worker(void*) {
    connectWifi();
    Job job;
    for (;;) {
        if (xQueueReceive(g_jobs, &job, portMAX_DELAY) == pdTRUE) {
            g_busy = true;
            runJob(job);
            g_busy = false;
        }
    }
}

}  // namespace

void begin(const store::Config& cfg) {
    g_cfg = cfg;
    if (g_task) return;
    g_jobs = xQueueCreate(kQueueLen, sizeof(Job));
    g_replies = xQueueCreate(1, sizeof(Reply));   // overwrite semantics
    g_status.link = LinkState::Connecting;
    // Pinned to core 0 so the UI task on core 1 keeps its frame time even
    // while TLS-free HTTP and Wi-Fi housekeeping run.
    // 12 KB: HTTPClient plus the Wi-Fi stack is not cheap, and a stack
    // overflow here would look like an unexplained reboot.
    xTaskCreatePinnedToCore(worker, "m5net", 12288, nullptr, 3, &g_task, 0);
}

bool requestDash() {
    if (!g_jobs) return false;
    Job j;
    j.isDash = true;
    j.overlayToken = 0;
    j.body[0] = 0;
    return xQueueSend(g_jobs, &j, 0) == pdTRUE;
}

uint32_t requestAction(const char* body, uint32_t overlayToken) {
    if (!g_jobs || !body) return 0;
    Job j;
    j.isDash = false;
    j.overlayToken = overlayToken;
    strncpy(j.body, body, sizeof(j.body) - 1);
    j.body[sizeof(j.body) - 1] = 0;
    // Queued, not sent: a press during a reconnect is accepted and goes out
    // as soon as the link is up. Swallowing input is what makes a remote
    // feel slower than it is.
    return xQueueSend(g_jobs, &j, 0) == pdTRUE ? overlayToken : 0;
}

bool takeResult(Result& out) {
    if (!g_replies) return false;
    if (xQueueReceive(g_replies, &g_lastReply, 0) != pdTRUE) return false;
    out.isDash = g_lastReply.isDash;
    out.ok = g_lastReply.ok;
    out.overlayToken = g_lastReply.overlayToken;
    out.body = g_lastReply.body;
    out.len = g_lastReply.len;
    out.status = g_lastReply.status;
    return true;
}

Status status() { return g_status; }
bool busy() { return g_busy || (g_jobs && uxQueueMessagesWaiting(g_jobs) > 0); }

void prepareForSleep() {
    WiFi.disconnect(true, false);     // keep the stored config
    WiFi.mode(WIFI_OFF);
}

}  // namespace net
