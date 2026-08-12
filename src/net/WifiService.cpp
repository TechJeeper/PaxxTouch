#include "net/WifiService.h"

#include <esp_heap_caps.h>
#include <lvgl.h>

bool WifiService::startConnect(const char *ssid, const char *password, int timeoutSec) {
    if (!ssid || !ssid[0]) return false;

    strlcpy(lastSsid_, ssid, sizeof(lastSsid_));
    if (password) strlcpy(lastPass_, password, sizeof(lastPass_));
    else lastPass_[0] = '\0';

    Serial.printf("[WiFi] connect ssid=\"%s\" timeout=%ds\n", ssid, timeoutSec);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(WIFI_PS_NONE);
    WiFi.disconnect(true);
    delay(50);
    WiFi.begin(ssid, password);

    connectPending_ = true;
    connectStartMs_ = millis();
    connectTimeoutSec_ = timeoutSec;
    return true;
}

void WifiService::finishConnect(bool ok, const char *message) {
    connectPending_ = false;
    lastConnected_ = ok && WiFi.status() == WL_CONNECTED;
    if (ok) {
        Serial.printf("[WiFi] connected ip=%s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.printf("[WiFi] connect failed status=%d internal_free=%u\n",
                      static_cast<int>(WiFi.status()),
                      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
    }
    if (statusCb_) statusCb_(ok, message);
}

bool WifiService::connect(const char *ssid, const char *password, int timeoutSec) {
    if (!startConnect(ssid, password, timeoutSec)) return false;

    const unsigned long deadline = millis() + static_cast<unsigned long>(timeoutSec) * 1000UL;
    while (millis() < deadline) {
        if (WiFi.status() == WL_CONNECTED) {
            finishConnect(true, localIp());
            return true;
        }
        delay(50);
        loop();
    }

    finishConnect(false, "WiFi connection failed");
    return false;
}

void WifiService::disconnect() {
    Serial.println("[WiFi] disconnect");
    connectPending_ = false;
    WiFi.disconnect(true);
}

void WifiService::forgetAll() {
    Serial.println("[WiFi] forget all saved networks");
    connectPending_ = false;
    lastSsid_[0] = '\0';
    lastPass_[0] = '\0';
    WiFi.disconnect(true);
}

const char *WifiService::localIp() const {
    static char buf[16];
    if (WiFi.status() == WL_CONNECTED) {
        snprintf(buf, sizeof(buf), "%s", WiFi.localIP().toString().c_str());
    } else {
        buf[0] = '\0';
    }
    return buf;
}

int WifiService::runScan(bool showHidden) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(WIFI_PS_NONE);

    int pending = WiFi.scanComplete();
    if (pending == WIFI_SCAN_RUNNING) {
        WiFi.scanDelete();
        delay(100);
    } else if (pending >= 0) {
        WiFi.scanDelete();
    }

    // Synchronous active scan, 500 ms per channel — reliable when not associated.
    return WiFi.scanNetworks(false, showHidden, false, 500);
}

void WifiService::scan(std::vector<WifiNetwork> &out) {
    out.clear();

    Serial.println("[WiFi] scan start");
    const bool wasConnected = WiFi.status() == WL_CONNECTED;
    char reconnectSsid[33] = {};
    char reconnectPass[64] = {};
    if (wasConnected) {
        strlcpy(reconnectSsid, WiFi.SSID().c_str(), sizeof(reconnectSsid));
        String psk = WiFi.psk();
        if (psk.length()) strlcpy(reconnectPass, psk.c_str(), sizeof(reconnectPass));
    } else if (lastSsid_[0]) {
        strlcpy(reconnectSsid, lastSsid_, sizeof(reconnectSsid));
        strlcpy(reconnectPass, lastPass_, sizeof(reconnectPass));
    }

    if (wasConnected) {
        Serial.println("[WiFi] pausing connection for scan");
        WiFi.disconnect(false, false);
        delay(250);
    }

    int n = runScan(true);
    Serial.printf("[WiFi] scanNetworks(sync) -> %d\n", n);

    if (n == WIFI_SCAN_FAILED) {
        Serial.println("[WiFi] scan failed — retrying");
        delay(200);
        n = runScan(true);
        Serial.printf("[WiFi] scanNetworks(retry) -> %d\n", n);
    }

    if (n > 0) {
        out.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            WifiNetwork net{};
            strlcpy(net.ssid, WiFi.SSID(i).c_str(), sizeof(net.ssid));
            net.rssi = WiFi.RSSI(i);
            net.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
            if (!net.ssid[0]) continue;
            out.push_back(net);
            Serial.printf("[WiFi]   [%d] \"%s\" rssi=%d secure=%d\n",
                          i, net.ssid, net.rssi, net.secure ? 1 : 0);
        }
    }
    WiFi.scanDelete();

    if (reconnectSsid[0]) {
        Serial.printf("[WiFi] reconnecting to \"%s\"\n", reconnectSsid);
        startConnect(reconnectSsid, reconnectPass, 15);
    }

    Serial.printf("[WiFi] scan done, %u network(s)\n", static_cast<unsigned>(out.size()));
}

void WifiService::loop() {
    if (connectPending_) {
        if (WiFi.status() == WL_CONNECTED) {
            finishConnect(true, localIp());
        } else if (millis() - connectStartMs_ > static_cast<unsigned long>(connectTimeoutSec_) * 1000UL) {
            finishConnect(false, "WiFi connection failed");
        }
    }

    const bool connected = isConnected();
    if (connected) {
        WiFi.setSleep(WIFI_PS_NONE);
    }
    if (connected != lastConnected_) {
        lastConnected_ = connected;
        if (statusCb_) {
            statusCb_(connected, connected ? localIp() : "WiFi disconnected");
        }
    }
}
