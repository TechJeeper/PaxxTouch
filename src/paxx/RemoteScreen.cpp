#include "paxx/RemoteScreen.h"

#include <esp_heap_caps.h>

namespace {

bool looksLikePng(const uint8_t *data, size_t len) {
    return len >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G';
}

bool looksLikeJpeg(const uint8_t *data, size_t len) {
    return len >= 2 && data[0] == 0xFF && data[1] == 0xD8;
}

const char *touchActionName(RemoteTouchAction action) {
    switch (action) {
        case RemoteTouchAction::Down: return "down";
        case RemoteTouchAction::Move: return "move";
        default: return "up";
    }
}

}  // namespace

void RemoteScreenClient::begin(const char *host, bool useAuth, const char *user, const char *pass, const char *apiKey) {
    strlcpy(host_, host, sizeof(host_));
    useAuth_ = useAuth;
    if (user) strlcpy(user_, user, sizeof(user_));
    if (pass) strlcpy(pass_, pass, sizeof(pass_));

    http_.configure(host, 80);
    http_.setAuth(useAuth_, user_, pass_);
    touchHttp_.configure(host, 80);
    touchHttp_.setAuth(useAuth_, user_, pass_);
    probeHttp_.configure(host, 80);
    probeHttp_.setAuth(useAuth_, user_, pass_);

    if (apiKey && apiKey[0]) {
        http_.setApiKey(apiKey);
        http_.setToken(nullptr);
        probeHttp_.setApiKey(apiKey);
        probeHttp_.setToken(nullptr);
        touchHttp_.setApiKey(apiKey);
        touchHttp_.setToken(nullptr);
    } else {
        http_.setApiKey(nullptr);
        http_.setToken(nullptr);
        probeHttp_.setApiKey(nullptr);
        probeHttp_.setToken(nullptr);
        touchHttp_.setApiKey(nullptr);
        touchHttp_.setToken(nullptr);
    }

    if (!buffer_) {
        buffer_ = static_cast<uint8_t *>(heap_caps_malloc(kBufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!buffer_) buffer_ = static_cast<uint8_t *>(malloc(kBufferSize));
    }

    if (!frameMutex_) frameMutex_ = xSemaphoreCreateMutex();
    if (!httpMutex_) httpMutex_ = xSemaphoreCreateMutex();

    if (!touchQueue_) {
        touchQueue_ = xQueueCreate(16, sizeof(TouchPoint));
        xTaskCreatePinnedToCore(touchWorker, "remoteTouch", 4096, this, 9, nullptr, 1);
    }

    if (!snapshotTask_) {
        xTaskCreatePinnedToCore(snapshotWorker, "remoteSnap", 12288, this, 2, &snapshotTask_, 0);
    }
}

void RemoteScreenClient::queueTouch(int u1X, int u1Y, RemoteTouchAction action) {
    if (host_[0] == '\0' || !enabled_ || !touchQueue_) return;

    abortSnapshot_ = true;
    lastTouchActivityMs_ = millis();

    const TouchPoint pt{
        static_cast<int16_t>(constrain(u1X, 0, U1_WIDTH - 1)),
        static_cast<int16_t>(constrain(u1Y, 0, U1_HEIGHT - 1)),
        action,
    };

    if (xQueueSend(touchQueue_, &pt, 0) != pdTRUE) {
        TouchPoint drop{};
        while (xQueueReceive(touchQueue_, &drop, 0) == pdTRUE) {
            if (drop.action == RemoteTouchAction::Down || drop.action == RemoteTouchAction::Up) {
                xQueueSendToFront(touchQueue_, &drop, 0);
                break;
            }
        }
        xQueueSend(touchQueue_, &pt, 0);
    }
}

void RemoteScreenClient::sendTouchEvent(RemoteScreenClient *self, const TouchPoint &pt) {
    char path[80];
    snprintf(path, sizeof(path), "/screen/touch?a=%s&x=%d&y=%d",
             touchActionName(pt.action), pt.x, pt.y);
    Serial.printf("[Remote] touch %s %d,%d\n", touchActionName(pt.action), pt.x, pt.y);
    if (!self->touchHttp_.postTouchFast(self->touchClient_, path)) {
        Serial.println("[Remote] touch POST failed");
    }
}

void RemoteScreenClient::flushTouchBatch(RemoteScreenClient *self, TouchPoint &down, TouchPoint &move, TouchPoint &up,
                                         bool &hasDown, bool &hasMove, bool &hasUp) {
    if (hasDown) {
        sendTouchEvent(self, down);
        hasDown = false;
    }
    if (hasMove) {
        sendTouchEvent(self, move);
        hasMove = false;
    }
    if (hasUp) {
        sendTouchEvent(self, up);
        hasUp = false;
    }
}

void RemoteScreenClient::touchWorker(void *arg) {
    auto *self = static_cast<RemoteScreenClient *>(arg);
    TouchPoint pt{};

    for (;;) {
        if (xQueueReceive(self->touchQueue_, &pt, portMAX_DELAY) != pdTRUE) continue;

        TouchPoint down{};
        TouchPoint move{};
        TouchPoint up{};
        bool hasDown = false;
        bool hasMove = false;
        bool hasUp = false;

        auto absorb = [&](const TouchPoint &p) {
            switch (p.action) {
                case RemoteTouchAction::Down:
                    down = p;
                    hasDown = true;
                    break;
                case RemoteTouchAction::Move:
                    move = p;
                    hasMove = true;
                    break;
                default:
                    up = p;
                    hasUp = true;
                    break;
            }
        };

        absorb(pt);

        for (;;) {
            TouchPoint next{};
            if (xQueueReceive(self->touchQueue_, &next, pdMS_TO_TICKS(12)) != pdTRUE) break;
            if (next.action == RemoteTouchAction::Up) {
                absorb(next);
                break;
            }
            absorb(next);
        }

        flushTouchBatch(self, down, move, up, hasDown, hasMove, hasUp);
    }
}

bool RemoteScreenClient::shouldDeferSnapshot() const {
    if (!touchQueue_) return false;
    if (uxQueueMessagesWaiting(touchQueue_) > 0) return true;
    return lastTouchActivityMs_ != 0 && millis() - lastTouchActivityMs_ < 2500;
}

void RemoteScreenClient::pumpSnapshot() {
    if (!snapshotTask_ || !enabled_) return;
    xTaskNotifyGive(snapshotTask_);
}

void RemoteScreenClient::discardPendingFrame() {
    if (!frameMutex_) return;
    xSemaphoreTake(frameMutex_, portMAX_DELAY);
    if (pendingPixels_) ImageDecoder::freeBuffer(pendingPixels_);
    pendingPixels_ = nullptr;
    pendingFormat_ = LV_COLOR_FORMAT_UNKNOWN;
    pendingW_ = pendingH_ = 0;
    pendingReady_ = false;
    xSemaphoreGive(frameMutex_);
}

void RemoteScreenClient::publishFrame(uint8_t *pixels, lv_color_format_t format, int w, int h) {
    if (!frameMutex_) {
        ImageDecoder::freeBuffer(pixels);
        return;
    }
    xSemaphoreTake(frameMutex_, portMAX_DELAY);
    if (pendingPixels_) ImageDecoder::freeBuffer(pendingPixels_);
    pendingPixels_ = pixels;
    pendingFormat_ = format;
    pendingW_ = w;
    pendingH_ = h;
    pendingReady_ = true;
    xSemaphoreGive(frameMutex_);
}

bool RemoteScreenClient::pollFrame(uint8_t *&pixels, lv_color_format_t &format, int &w, int &h) {
    pixels = nullptr;
    format = LV_COLOR_FORMAT_UNKNOWN;
    w = h = 0;
    if (!frameMutex_) return false;

    xSemaphoreTake(frameMutex_, portMAX_DELAY);
    if (!pendingReady_ || !pendingPixels_) {
        xSemaphoreGive(frameMutex_);
        return false;
    }

    pixels = pendingPixels_;
    format = pendingFormat_;
    w = pendingW_;
    h = pendingH_;
    pendingPixels_ = nullptr;
    pendingReady_ = false;
    pendingFormat_ = LV_COLOR_FORMAT_UNKNOWN;
    pendingW_ = pendingH_ = 0;
    xSemaphoreGive(frameMutex_);
    return true;
}

void RemoteScreenClient::resetProbe() {
    if (host_[0] == '\0' || !enabled_) {
        probeState_ = RemoteProbeState::Failed;
        strlcpy(probeError_, "Printer host not configured", sizeof(probeError_));
        return;
    }
    if (probeState_ == RemoteProbeState::Running) return;

    probeState_ = RemoteProbeState::Running;
    probeError_[0] = '\0';

    if (probeTask_ == nullptr) {
        xTaskCreate(probeWorker, "remoteProbe", 8192, this, 5, &probeTask_);
    }
    if (probeTask_ != nullptr) {
        xTaskNotifyGive(probeTask_);
    }
}

void RemoteScreenClient::probeWorker(void *arg) {
    auto *self = static_cast<RemoteScreenClient *>(arg);

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        const bool ok = self->probeAvailable();
        if (ok) {
            self->probeState_ = RemoteProbeState::Ok;
            self->probeError_[0] = '\0';
            self->lastSnapshotMs_ = 0;
            Serial.println("[Remote] probe OK");
            if (self->snapshotTask_) xTaskNotifyGive(self->snapshotTask_);
        } else {
            self->probeState_ = RemoteProbeState::Failed;
            snprintf(self->probeError_, sizeof(self->probeError_),
                     "Cannot reach /screen/ (HTTP %d)\nCheck printer IP & Remote Screen setting",
                     self->probeHttp_.statusCode() ? self->probeHttp_.statusCode() : self->http_.statusCode());
            Serial.printf("[Remote] probe failed http=%d\n", self->probeHttp_.statusCode());
        }
    }
}

bool RemoteScreenClient::downloadSnapshot(int &outLen, bool forProbe) {
    outLen = 0;
    if (!buffer_ || (!forProbe && shouldDeferSnapshot())) return false;
    if (!httpMutex_) return false;

    xSemaphoreTake(httpMutex_, portMAX_DELAY);
    abortSnapshot_ = false;
    char path[64];
    snprintf(path, sizeof(path), "/screen/snapshot?t=%lu", static_cast<unsigned long>(millis()));
    outLen = http_.download(path, buffer_, kBufferSize, forProbe ? 3000 : 5000, forProbe ? nullptr : &abortSnapshot_);
    xSemaphoreGive(httpMutex_);

    if (outLen > 0) return true;
    if (abortSnapshot_) return false;

    Serial.printf("[Remote] snapshot failed http=%d\n", http_.statusCode());
    return false;
}

bool RemoteScreenClient::decodeSnapshot(int len, uint8_t *&pixels, lv_color_format_t &format, int &w, int &h) {
    pixels = nullptr;
    format = LV_COLOR_FORMAT_UNKNOWN;
    w = h = 0;
    if (len <= 0 || shouldDeferSnapshot()) return false;

    const size_t bytes = static_cast<size_t>(len);
    if (looksLikePng(buffer_, bytes)) {
        format = LV_COLOR_FORMAT_RGB888;
        return ImageDecoder::decodePng(buffer_, bytes, pixels, w, h);
    }
    if (looksLikeJpeg(buffer_, bytes)) {
        uint16_t *rgb565 = nullptr;
        if (!ImageDecoder::decodeJpeg(buffer_, bytes, rgb565, w, h)) return false;
        pixels = reinterpret_cast<uint8_t *>(rgb565);
        format = LV_COLOR_FORMAT_RGB565;
        return true;
    }

    Serial.printf("[Remote] unknown snapshot format (first bytes %02x %02x) len=%d\n",
                  buffer_[0], buffer_[1], len);
    return false;
}

void RemoteScreenClient::snapshotWorker(void *arg) {
    auto *self = static_cast<RemoteScreenClient *>(arg);

    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));

        if (!self->enabled_ || !self->buffer_) continue;
        if (self->shouldDeferSnapshot()) continue;
        if (self->snapshotBusy_) continue;
        if (millis() - self->lastSnapshotMs_ < self->refreshIntervalMs_) continue;

        self->snapshotBusy_ = true;
        int len = 0;
        const bool downloaded = self->downloadSnapshot(len);
        if (downloaded && len > 0 && !self->abortSnapshot_) {
            uint8_t *pixels = nullptr;
            lv_color_format_t format = LV_COLOR_FORMAT_UNKNOWN;
            int w = 0;
            int h = 0;
            if (self->decodeSnapshot(len, pixels, format, w, h) && pixels && !self->shouldDeferSnapshot()) {
                self->publishFrame(pixels, format, w, h);
                self->lastSnapshotMs_ = millis();
                Serial.printf("[Remote] frame ready %dx%d\n", w, h);
            } else if (pixels) {
                ImageDecoder::freeBuffer(pixels);
            }
        }
        self->snapshotBusy_ = false;
        self->abortSnapshot_ = false;
    }
}

bool RemoteScreenClient::probeAvailable() {
    if (host_[0] == '\0' || !enabled_ || !buffer_ || !httpMutex_) return false;

    xSemaphoreTake(httpMutex_, portMAX_DELAY);

    int len = 0;
    abortSnapshot_ = false;
    char path[64];
    snprintf(path, sizeof(path), "/screen/snapshot?t=%lu", static_cast<unsigned long>(millis()));
    len = probeHttp_.download(path, buffer_, kBufferSize, 8000);

    bool ok = false;
    if (len > 0 && (looksLikePng(buffer_, static_cast<size_t>(len)) ||
                    looksLikeJpeg(buffer_, static_cast<size_t>(len)))) {
        Serial.printf("[Remote] probe OK snapshot (%d bytes)\n", len);
        ok = true;
    } else {
        String out;
        if (probeHttp_.getString("/screen/", out, 5000) && out.length() > 0) {
            Serial.println("[Remote] probe via /screen/ HTML OK");
            ok = true;
        } else {
            Serial.printf("[Remote] probe failed http=%d len=%d\n", probeHttp_.statusCode(), len);
        }
    }

    xSemaphoreGive(httpMutex_);
    return ok;
}
