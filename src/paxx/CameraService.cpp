#include "paxx/CameraService.h"

#include <esp_heap_caps.h>

void CameraService::begin(const char *host, bool useAuth, const char *user, const char *pass, const char *apiKey) {
    strlcpy(host_, host, sizeof(host_));
    useAuth_ = useAuth;
    if (user) strlcpy(user_, user, sizeof(user_));
    if (pass) strlcpy(pass_, pass, sizeof(pass_));

    http_.configure(host, 80);
    http_.setAuth(useAuth, user, pass);
    if (apiKey && apiKey[0]) {
        http_.setApiKey(apiKey);
        http_.setToken(nullptr);
    } else {
        http_.setApiKey(nullptr);
        http_.setToken(nullptr);
    }
    if (!buffer_) {
        buffer_ = static_cast<uint8_t *>(heap_caps_malloc(kBufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!buffer_) buffer_ = static_cast<uint8_t *>(malloc(kBufferSize));
    }
}

bool CameraService::fetchSnapshot(uint16_t *&rgb565, int &w, int &h) {
    rgb565 = nullptr;
    w = h = 0;
    if (!buffer_ || host_[0] == '\0') return false;

    char path[96];
    const char *paths[] = {
        "/webcam/snapshot.jpg",
        "/webcam/snapshot",
        "/webcam/?action=snapshot",
    };

    for (const char *base : paths) {
        snprintf(path, sizeof(path), "%s?t=%lu", base, static_cast<unsigned long>(millis()));
        const int len = http_.download(path, buffer_, kBufferSize, 10000);
        if (len <= 0) {
            Serial.printf("[Camera] %s failed http=%d\n", base, http_.statusCode());
            continue;
        }
        if (ImageDecoder::decodeJpeg(buffer_, static_cast<size_t>(len), rgb565, w, h)) {
            Serial.printf("[Camera] OK via %s %dx%d\n", base, w, h);
            return true;
        }
        Serial.printf("[Camera] decode failed for %s (%d bytes)\n", base, len);
    }

    return false;
}
