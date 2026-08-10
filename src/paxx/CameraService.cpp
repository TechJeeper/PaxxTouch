#include "paxx/CameraService.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

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
    if (!mutex_) mutex_ = xSemaphoreCreateMutex();
    if (!workerTask_) {
        xTaskCreatePinnedToCore(worker, "camera", 12288, this, 3, &workerTask_, 0);
    }
}

void CameraService::requestFetch() {
    if (!mutex_ || host_[0] == '\0') return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    fetchPending_ = true;
    xSemaphoreGive(mutex_);
    if (workerTask_) xTaskNotifyGive(workerTask_);
}

bool CameraService::poll(uint16_t *&rgb565, int &w, int &h) {
    rgb565 = nullptr;
    w = h = 0;
    if (!mutex_) return false;

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!ready_ || !readyPixels_) {
        xSemaphoreGive(mutex_);
        return false;
    }

    rgb565 = readyPixels_;
    w = readyW_;
    h = readyH_;
    readyPixels_ = nullptr;
    ready_ = false;
    xSemaphoreGive(mutex_);
    return true;
}

bool CameraService::fetchSnapshotInternal(uint16_t *&rgb565, int &w, int &h) {
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
        const int len = http_.download(path, buffer_, kBufferSize, 12000);
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

void CameraService::worker(void *arg) {
    auto *self = static_cast<CameraService *>(arg);

    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));

        bool pending = false;
        xSemaphoreTake(self->mutex_, portMAX_DELAY);
        pending = self->fetchPending_;
        self->fetchPending_ = false;
        if (self->readyPixels_) {
            ImageDecoder::freeBuffer(self->readyPixels_);
            self->readyPixels_ = nullptr;
        }
        self->ready_ = false;
        xSemaphoreGive(self->mutex_);

        if (!pending) continue;

        uint16_t *pixels = nullptr;
        int w = 0;
        int h = 0;
        const bool ok = self->fetchSnapshotInternal(pixels, w, h);

        xSemaphoreTake(self->mutex_, portMAX_DELAY);
        if (ok && pixels) {
            self->readyPixels_ = pixels;
            self->readyW_ = w;
            self->readyH_ = h;
            self->ready_ = true;
        }
        xSemaphoreGive(self->mutex_);
    }
}

bool CameraService::fetchSnapshot(uint16_t *&rgb565, int &w, int &h) {
    return fetchSnapshotInternal(rgb565, w, h);
}
