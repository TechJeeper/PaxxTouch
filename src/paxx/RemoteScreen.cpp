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



uint8_t *allocBuffer(size_t bytes) {

    uint8_t *p = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    if (!p) p = static_cast<uint8_t *>(malloc(bytes));

    return p;

}



uint16_t *allocDisplayBuffer() {

    const size_t bytes = static_cast<size_t>(RemoteScreenClient::U1_WIDTH) *

                         RemoteScreenClient::U1_HEIGHT * sizeof(uint16_t);

    uint16_t *p = static_cast<uint16_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    if (!p) p = static_cast<uint16_t *>(malloc(bytes));

    return p;

}



}  // namespace



void RemoteScreenClient::begin(const char *host, bool useAuth, const char *user, const char *pass,

                               const char *apiKey, const char *token) {

    strlcpy(host_, host, sizeof(host_));

    useAuth_ = useAuth;

    if (user) strlcpy(user_, user, sizeof(user_));

    if (pass) strlcpy(pass_, pass, sizeof(pass_));



    applyHttpEndpoint(80, "/screen/", "/screen/touch");



    if (apiKey && apiKey[0]) {

        strlcpy(apiKey_, apiKey, sizeof(apiKey_));

        token_[0] = '\0';

        http_.setApiKey(apiKey);

        probeHttp_.setApiKey(apiKey);

        touchHttp_.setApiKey(apiKey);

    } else if (token && token[0]) {

        strlcpy(token_, token, sizeof(token_));

        apiKey_[0] = '\0';

        http_.setToken(token);

        probeHttp_.setToken(token);

        touchHttp_.setToken(token);

    } else {

        apiKey_[0] = '\0';

        token_[0] = '\0';

    }



    if (!fetchBuf_) fetchBuf_ = allocBuffer(kBufferSize);
    if (!readyBuf_) readyBuf_ = allocBuffer(kBufferSize);
    if (!decodeBuf_) decodeBuf_ = allocBuffer(kBufferSize);
    if (!displayBuf_) displayBuf_ = allocDisplayBuffer();
    if (!fetchBuf_ || !readyBuf_ || !decodeBuf_ || !displayBuf_) {
        Serial.println("[Remote] FATAL: buffer alloc failed");
    }

    if (!fetchMutex_) fetchMutex_ = xSemaphoreCreateMutex();
    if (!readyMutex_) readyMutex_ = xSemaphoreCreateMutex();
    if (!frameMutex_) frameMutex_ = xSemaphoreCreateMutex();
}



void RemoteScreenClient::applyHttpEndpoint(uint16_t port, const char *snapPath, const char *touchPrefix) {

    strlcpy(snapshotPath_, snapPath, sizeof(snapshotPath_));

    strlcpy(touchPathPrefix_, touchPrefix, sizeof(touchPathPrefix_));

    http_.configure(host_, port);

    probeHttp_.configure(host_, port);

    touchHttp_.configure(host_, port);

    http_.setAuth(useAuth_, user_, pass_);

    probeHttp_.setAuth(useAuth_, user_, pass_);

    touchHttp_.setAuth(useAuth_, user_, pass_);

    if (apiKey_[0]) {

        http_.setApiKey(apiKey_);

        probeHttp_.setApiKey(apiKey_);

        touchHttp_.setApiKey(apiKey_);

    } else if (token_[0]) {

        http_.setToken(token_);

        probeHttp_.setToken(token_);

        touchHttp_.setToken(token_);

    }

}



void RemoteScreenClient::ensureWorkers() {

    if (!touchQueue_) {

        touchQueue_ = xQueueCreate(24, sizeof(TouchPoint));

        if (touchQueue_) {

            xTaskCreatePinnedToCore(touchWorker, "remoteTouch", 4096, this, 10, nullptr, 1);

        }

    }

    if (!pollTask_) {
        if (xTaskCreatePinnedToCore(pollWorker, "remotePoll", 6144, this, 7, &pollTask_, 0) != pdPASS) {
            Serial.println("[Remote] poll task create failed");
            pollTask_ = nullptr;
        }
    }
    if (!decodeTask_) {
        ImageDecoder::initWorker();
        if (xTaskCreatePinnedToCore(decodeWorker, "remoteDec", 6144, this, 6, &decodeTask_, 1) != pdPASS) {
            Serial.println("[Remote] decode task create failed");
            decodeTask_ = nullptr;
        }
    }
}



void RemoteScreenClient::setViewActive(bool active) {

    viewActive_ = active;

    if (active) {
        ensureWorkers();
    } else {
        probeState_ = RemoteProbeState::Idle;
        snapshotEtag_[0] = '\0';

        notModifiedPolls_ = 0;

        snapshotPolls_ = 0;

        readyDecodeLen_ = 0;

        frameDirty_ = false;

        snapshotClient_.stop();

        touchClient_.stop();

    }

}



void RemoteScreenClient::queueTouch(int u1X, int u1Y, RemoteTouchAction action) {

    if (host_[0] == '\0' || !enabled_ || !touchQueue_) return;



    const TouchPoint pt{

        static_cast<int16_t>(constrain(u1X, 0, U1_WIDTH - 1)),

        static_cast<int16_t>(constrain(u1Y, 0, U1_HEIGHT - 1)),

        action,

    };



    if (xQueueSend(touchQueue_, &pt, 0) != pdTRUE) {

        TouchPoint drop{};

        while (xQueueReceive(touchQueue_, &drop, 0) == pdTRUE) {

            if (drop.action == RemoteTouchAction::Move) continue;

            xQueueSendToFront(touchQueue_, &drop, 0);

            break;

        }

        xQueueSend(touchQueue_, &pt, 0);

    }

}



void RemoteScreenClient::sendTouchHttp(RemoteScreenClient *self, const TouchPoint &pt) {

    char path[80];

    snprintf(path, sizeof(path), "%s?a=%s&x=%d&y=%d",

             self->touchPathPrefix_, touchActionName(pt.action), pt.x, pt.y);

    if (!self->touchHttp_.postTouchFireAndForget(self->touchClient_, path)) {
        static unsigned touchFailLogMs = 0;
        const unsigned long now = millis();
        if (now - touchFailLogMs > 2000) {
            touchFailLogMs = now;
            Serial.printf("[Remote] touch send failed (%s)\n", path);
        }
        self->touchClient_.stop();
    }

}



void RemoteScreenClient::touchWorker(void *arg) {

    auto *self = static_cast<RemoteScreenClient *>(arg);

    TouchPoint pt{};



    for (;;) {

        if (!self->touchQueue_) {

            vTaskDelay(pdMS_TO_TICKS(200));

            continue;

        }

        if (xQueueReceive(self->touchQueue_, &pt, portMAX_DELAY) != pdTRUE) continue;



        if (pt.action == RemoteTouchAction::Down || pt.action == RemoteTouchAction::Up) {
            self->fastPollUntilMs_ = millis() + 1500;
            self->snapshotEtag_[0] = '\0';
            sendTouchHttp(self, pt);
            if (self->viewActive_) self->pumpSnapshot();
            continue;
        }

        TouchPoint move = pt;
        for (;;) {
            TouchPoint next{};
            if (xQueueReceive(self->touchQueue_, &next, pdMS_TO_TICKS(8)) != pdTRUE) break;
            if (next.action == RemoteTouchAction::Down) {
                xQueueSendToFront(self->touchQueue_, &next, 0);
                break;
            }
            if (next.action == RemoteTouchAction::Up) {
                self->fastPollUntilMs_ = millis() + 1500;
                self->snapshotEtag_[0] = '\0';
                sendTouchHttp(self, move);
                sendTouchHttp(self, next);
                if (self->viewActive_) self->pumpSnapshot();
                goto next_batch;
            }
            move = next;
        }
        self->fastPollUntilMs_ = millis() + 1500;
        sendTouchHttp(self, move);
    next_batch:;
    }
}



SnapshotFetchStatus RemoteScreenClient::pollSnapshot(int &outLen) {

    outLen = 0;

    if (!fetchBuf_ || !fetchMutex_) return SnapshotFetchStatus::Error;



    xSemaphoreTake(fetchMutex_, portMAX_DELAY);

    const char *ifNoneMatch = snapshotEtag_[0] ? snapshotEtag_ : nullptr;

    const SnapshotFetchStatus st = http_.fetchSnapshot(

        snapshotPath_, fetchBuf_, kBufferSize, 1500, nullptr, snapshotClient_,

        ifNoneMatch, snapshotEtag_, sizeof(snapshotEtag_), outLen, true);

    xSemaphoreGive(fetchMutex_);

    return st;

}



void RemoteScreenClient::signalDecode(int len) {
    if (len <= 0 || !fetchBuf_ || !readyBuf_ || !readyMutex_) return;

    xSemaphoreTake(readyMutex_, portMAX_DELAY);
    uint8_t *tmp = fetchBuf_;
    fetchBuf_ = readyBuf_;
    readyBuf_ = tmp;
    readyDecodeLen_ = len;
    xSemaphoreGive(readyMutex_);

    if (decodeTask_) xTaskNotifyGive(decodeTask_);
}



bool RemoteScreenClient::decodeFromDecodeBuffer(int len) {
    if (len <= 0 || len > static_cast<int>(kBufferSize) || !decodeBuf_ || !displayBuf_) return false;

    const uint8_t *src = decodeBuf_;
    const size_t bytes = static_cast<size_t>(len);
    int w = 0;
    int h = 0;

    if (looksLikePng(src, bytes)) {
        if (ImageDecoder::decodePngRgb565IntoSync(src, bytes, displayBuf_, w, h)) {
            displayW_ = w;
            displayH_ = h;
            displayFormat_ = LV_COLOR_FORMAT_RGB565;
            return true;
        }
        snprintf(snapshotError_, sizeof(snapshotError_), "PNG decode failed");
        return false;
    }
    if (looksLikeJpeg(src, bytes)) {
        if (ImageDecoder::decodeJpegIntoSync(src, bytes, displayBuf_, w, h)) {
            displayW_ = w;
            displayH_ = h;
            displayFormat_ = LV_COLOR_FORMAT_RGB565;
            return true;
        }
        snprintf(snapshotError_, sizeof(snapshotError_), "JPEG decode failed");
        return false;
    }
    snprintf(snapshotError_, sizeof(snapshotError_), "Unknown image format");
    return false;
}

bool RemoteScreenClient::decodeFromFetchBuffer(int len) {
    if (len <= 0 || len > static_cast<int>(kBufferSize) || !fetchBuf_ || !displayBuf_) return false;

    const uint8_t *src = fetchBuf_;
    const size_t bytes = static_cast<size_t>(len);
    int w = 0;
    int h = 0;

    if (looksLikePng(src, bytes)) {
        if (ImageDecoder::decodePngRgb565IntoSync(src, bytes, displayBuf_, w, h)) {
            displayW_ = w;
            displayH_ = h;
            displayFormat_ = LV_COLOR_FORMAT_RGB565;
            return true;
        }
        snprintf(snapshotError_, sizeof(snapshotError_), "PNG decode failed");
        return false;
    }
    if (looksLikeJpeg(src, bytes)) {
        if (ImageDecoder::decodeJpegIntoSync(src, bytes, displayBuf_, w, h)) {
            displayW_ = w;
            displayH_ = h;
            displayFormat_ = LV_COLOR_FORMAT_RGB565;
            return true;
        }
        snprintf(snapshotError_, sizeof(snapshotError_), "JPEG decode failed");
        return false;
    }
    snprintf(snapshotError_, sizeof(snapshotError_), "Unknown image format");
    return false;
}



void RemoteScreenClient::publishDisplayFrame() {

    if (!frameMutex_ || !displayBuf_ || displayW_ <= 0 || displayH_ <= 0) return;

    xSemaphoreTake(frameMutex_, portMAX_DELAY);

    frameDirty_ = true;

    xSemaphoreGive(frameMutex_);

}



void RemoteScreenClient::decodeWorker(void *arg) {
    auto *self = static_cast<RemoteScreenClient *>(arg);

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!self->viewActive_) continue;

        int len = 0;
        if (self->readyMutex_ && xSemaphoreTake(self->readyMutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
            len = self->readyDecodeLen_;
            if (len > 0) {
                uint8_t *tmp = self->readyBuf_;
                self->readyBuf_ = self->decodeBuf_;
                self->decodeBuf_ = tmp;
                self->readyDecodeLen_ = 0;
            }
            xSemaphoreGive(self->readyMutex_);
        }

        if (len > 0) {
            if (self->decodeFromDecodeBuffer(len)) {
                self->snapshotError_[0] = '\0';
                self->publishDisplayFrame();
            }
        }
    }
}

void RemoteScreenClient::pollWorker(void *arg) {
    auto *self = static_cast<RemoteScreenClient *>(arg);

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        Serial.println("[Remote] poll loop started (HTTP keep-alive, pipelined decode)");

        while (self->viewActive_ && self->enabled_ && self->fetchBuf_) {
            const uint32_t t0 = millis();
            int len = 0;
            const SnapshotFetchStatus st = self->pollSnapshot(len);

            if (st == SnapshotFetchStatus::NotModified) {
                self->notModifiedPolls_++;
                self->snapshotPolls_++;
            } else if (st == SnapshotFetchStatus::Ok && len > 0) {
                self->notModifiedPolls_ = 0;
                self->snapshotPolls_++;
                self->signalDecode(len);
            } else if (st == SnapshotFetchStatus::Error) {
                if (self->http_.statusCode() != 304 && self->http_.statusCode() != 204) {
                    snprintf(self->snapshotError_, sizeof(self->snapshotError_),
                             "Snapshot HTTP %d", self->http_.statusCode());
                }
                self->snapshotClient_.stop();
                vTaskDelay(pdMS_TO_TICKS(50));
            }

            if ((self->snapshotPolls_ % 50) == 0 && self->snapshotPolls_ > 0) {
                Serial.printf("[Remote] poll #%u http=%d len=%d 304s=%u (%lums)\n",
                              self->snapshotPolls_, self->http_.statusCode(), len,
                              self->notModifiedPolls_, static_cast<unsigned long>(millis() - t0));
            }

            const bool fastMode = (millis() < self->fastPollUntilMs_);
            const unsigned long defaultInterval = self->refreshIntervalMs_ > 0 ? self->refreshIntervalMs_ : 100;
            const unsigned long interval = fastMode ? 20UL : defaultInterval;

            const uint32_t elapsed = millis() - t0;
            if (elapsed < interval) {
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(interval - elapsed));
            }
        }
    }
}



void RemoteScreenClient::pumpSnapshot() {

    if (!pollTask_ || !enabled_ || !viewActive_) return;

    xTaskNotifyGive(pollTask_);

}



bool RemoteScreenClient::pollFrame(uint8_t *&pixels, lv_color_format_t &format, int &w, int &h) {

    pixels = nullptr;

    format = LV_COLOR_FORMAT_UNKNOWN;

    w = h = 0;

    if (!frameMutex_ || !displayBuf_) return false;



    xSemaphoreTake(frameMutex_, portMAX_DELAY);

    if (!frameDirty_) {

        xSemaphoreGive(frameMutex_);

        return false;

    }

    frameDirty_ = false;

    pixels = reinterpret_cast<uint8_t *>(displayBuf_);

    format = displayFormat_;

    w = displayW_;

    h = displayH_;

    xSemaphoreGive(frameMutex_);

    return true;

}



bool RemoteScreenClient::probeEndpoint(uint16_t port, const char *snapPath, const char *touchPrefix, int &outLen) {
    applyHttpEndpoint(port, snapPath, touchPrefix);
    snapshotClient_.stop();

    if (!fetchBuf_ || !fetchMutex_) return false;

    xSemaphoreTake(fetchMutex_, portMAX_DELAY);
    outLen = 0;
    snapshotEtag_[0] = '\0';
    const SnapshotFetchStatus st = probeHttp_.fetchSnapshot(
        snapPath, fetchBuf_, kBufferSize, 8000, nullptr, snapshotClient_,
        nullptr, snapshotEtag_, sizeof(snapshotEtag_), outLen);
    const bool ok = st == SnapshotFetchStatus::Ok && outLen > 0 &&
                    (looksLikePng(fetchBuf_, static_cast<size_t>(outLen)) ||
                     looksLikeJpeg(fetchBuf_, static_cast<size_t>(outLen)));
    xSemaphoreGive(fetchMutex_);

    if (ok) {
        Serial.printf("[Remote] probe OK %s:%u%s (%d bytes) etag=%s\n",
                      host_, port, snapPath, outLen, snapshotEtag_);
    } else {
        Serial.printf("[Remote] probe fail %s:%u%s http=%d len=%d\n",
                      host_, port, snapPath, probeHttp_.statusCode(), outLen);
    }
    return ok;
}



bool RemoteScreenClient::probeAvailable() {

    if (host_[0] == '\0' || !enabled_ || !fetchBuf_) return false;

    static const char *kSnapPaths[] = {
        "/screen/",
        "/screen",
        "/screen/snapshot",
        "/screen/snapshot.jpg",
    };

    int len = 0;
    for (const char *path : kSnapPaths) {
        if (probeEndpoint(80, path, "/screen/touch", len)) {
            lastProbeSnapshotLen_ = len;
            return true;
        }
    }

    const int code = probeHttp_.statusCode() ? probeHttp_.statusCode() : http_.statusCode();
    snprintf(probeError_, sizeof(probeError_),
             "No image from %s (HTTP %d)\nCheck IP / Remote Screen setting on U1",
             host_, code);

    applyHttpEndpoint(80, "/screen/", "/screen/touch");

    return false;

}



void RemoteScreenClient::resetProbe() {
    if (host_[0] == '\0' || !enabled_) {
        probeState_ = RemoteProbeState::Failed;
        strlcpy(probeError_, "Printer host not configured", sizeof(probeError_));
        return;
    }

    probeState_ = RemoteProbeState::Running;
    probeStartedMs_ = millis();
    probeError_[0] = '\0';
    lastProbeSnapshotLen_ = 0;
    Serial.printf("[Remote] probe start %s\n", host_);

    if (probeTask_ == nullptr) {
        if (xTaskCreate(probeWorker, "remoteProbe", 5120, this, 5, &probeTask_) != pdPASS) {
            Serial.println("[Remote] probe task create failed");
            probeTask_ = nullptr;
            probeState_ = RemoteProbeState::Failed;
            strlcpy(probeError_, "Remote screen probe failed to start", sizeof(probeError_));
            return;
        }
    }
    xTaskNotifyGive(probeTask_);
}



void RemoteScreenClient::forceProbeFailed(const char *message) {

    probeState_ = RemoteProbeState::Failed;

    probeStartedMs_ = 0;

    strlcpy(probeError_, message ? message : "Remote screen unavailable", sizeof(probeError_));

}



void RemoteScreenClient::probeWorker(void *arg) {

    auto *self = static_cast<RemoteScreenClient *>(arg);



    for (;;) {

        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);



        const bool ok = self->probeAvailable();
        if (ok) {
            if (self->lastProbeSnapshotLen_ > 0) {
                if (self->decodeFromFetchBuffer(self->lastProbeSnapshotLen_)) {
                    self->snapshotError_[0] = '\0';
                    self->publishDisplayFrame();
                    Serial.printf("[Remote] first frame %dx%d\n", self->displayW_, self->displayH_);
                } else {
                    Serial.printf("[Remote] first frame decode failed: %s\n", self->snapshotError_);
                    self->signalDecode(self->lastProbeSnapshotLen_);
                }
            }
            self->probeState_ = RemoteProbeState::Ok;
            self->probeError_[0] = '\0';
            Serial.printf("[Remote] ready — http://%s/screen/snapshot\n", self->host_);
            if (self->viewActive_) self->pumpSnapshot();
        } else {

            self->probeState_ = RemoteProbeState::Failed;

            Serial.printf("[Remote] probe failed http=%d\n", self->probeHttp_.statusCode());

        }

    }

}


