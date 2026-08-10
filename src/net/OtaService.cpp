#include "net/OtaService.h"

#include <ArduinoOTA.h>
#include <WiFi.h>

void OtaService::begin(const char *hostname) {
    if (started_ || WiFi.status() != WL_CONNECTED) return;

    ArduinoOTA.setHostname(hostname);
    ArduinoOTA.onStart([this]() {
        if (cb_) cb_("OTA update starting", 0);
    });
    ArduinoOTA.onProgress([this](unsigned int progress, unsigned int total) {
        if (cb_ && total > 0) {
            cb_("Updating firmware", static_cast<int>((progress * 100) / total));
        }
    });
    ArduinoOTA.onEnd([this]() {
        if (cb_) cb_("OTA complete, rebooting", 100);
    });
    ArduinoOTA.onError([this](ota_error_t err) {
        if (cb_) cb_("OTA update failed", static_cast<int>(err));
    });
    ArduinoOTA.begin();
    started_ = true;
}

void OtaService::loop() {
    if (started_) {
        ArduinoOTA.handle();
    }
}
