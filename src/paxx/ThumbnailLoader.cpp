#include "paxx/ThumbnailLoader.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>

#include "paxx/ImageDecoder.h"

namespace {

void urlEncodeAppend(const char *src, String &out) {
    for (const char *p = src; *p; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '/') {
            out += static_cast<char>(c);
        } else {
            char buf[8];
            snprintf(buf, sizeof(buf), "%%%.2X", c);
            out += buf;
        }
    }
}

bool pickBestThumbnail(JsonArray arr, char *thumbPath, size_t thumbPathLen) {
    int bestScore = -1;
    thumbPath[0] = '\0';

    for (JsonObject item : arr) {
        const int width = item["width"] | 0;
        const int height = item["height"] | 0;
        const char *path = item["thumbnail_path"] | item["relative_path"] | "";
        if (!path[0]) continue;

        int score = width * height;
        if (width > 240 || height > 240) score /= 4;
        if (width < 48 || height < 48) score /= 2;
        if (score > bestScore) {
            bestScore = score;
            strlcpy(thumbPath, path, thumbPathLen);
        }
    }
    return thumbPath[0] != '\0';
}

bool parseThumbnailList(const String &response, char *thumbPath, size_t thumbPathLen) {
    JsonDocument doc;
    if (deserializeJson(doc, response) != DeserializationError::Ok) return false;

    JsonArray arr;
    if (doc["result"].is<JsonArray>()) arr = doc["result"].as<JsonArray>();
    else if (doc.is<JsonArray>()) arr = doc.as<JsonArray>();
    if (arr.isNull()) return false;
    return pickBestThumbnail(arr, thumbPath, thumbPathLen);
}

void resolveMetadataThumbPath(const char *gcodePath, const char *relPath, char *out, size_t outLen) {
    if (strchr(relPath, '/')) {
        strlcpy(out, relPath, outLen);
        return;
    }
    const char *slash = strrchr(gcodePath, '/');
    if (slash) {
        const size_t dirLen = static_cast<size_t>(slash - gcodePath);
        if (dirLen + 1 + strlen(relPath) >= outLen) return;
        memcpy(out, gcodePath, dirLen);
        out[dirLen] = '/';
        strlcpy(out + dirLen + 1, relPath, outLen - dirLen - 1);
    } else {
        strlcpy(out, relPath, outLen);
    }
}

}  // namespace

void ThumbnailLoader::begin(const char *host, uint16_t port, bool useAuth, const char *user,
                            const char *pass, const char *apiKey) {
    http_.configure(host, port);
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
        xTaskCreatePinnedToCore(worker, "thumbLoad", 12288, this, 3, &workerTask_, 0);
    }
}

void ThumbnailLoader::clear() {
    if (!mutex_) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    hasPending_ = false;
    pendingPath_[0] = '\0';
    ready_ = false;
    readyFormat_ = LV_COLOR_FORMAT_UNKNOWN;
    if (readyPixels_) {
        ImageDecoder::freeBuffer(readyPixels_);
        readyPixels_ = nullptr;
    }
    readyW_ = readyH_ = 0;
    readyPath_[0] = '\0';
    xSemaphoreGive(mutex_);
}

void ThumbnailLoader::request(const char *gcodePath) {
    if (!gcodePath || !gcodePath[0] || !mutex_) return;

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (ready_ && strcmp(readyPath_, gcodePath) == 0) {
        xSemaphoreGive(mutex_);
        return;
    }
    if (busy_ && strcmp(pendingPath_, gcodePath) == 0) {
        xSemaphoreGive(mutex_);
        return;
    }
    strlcpy(pendingPath_, gcodePath, sizeof(pendingPath_));
    hasPending_ = true;
    xSemaphoreGive(mutex_);

    if (workerTask_) xTaskNotifyGive(workerTask_);
}

bool ThumbnailLoader::poll(void *&pixels, lv_color_format_t &format, int &w, int &h, char *pathOut,
                           size_t pathOutLen) {
    pixels = nullptr;
    format = LV_COLOR_FORMAT_UNKNOWN;
    w = h = 0;
    if (!mutex_) return false;

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!ready_ || !readyPixels_) {
        xSemaphoreGive(mutex_);
        return false;
    }

    pixels = readyPixels_;
    format = readyFormat_;
    w = readyW_;
    h = readyH_;
    if (pathOut && pathOutLen) strlcpy(pathOut, readyPath_, pathOutLen);
    readyPixels_ = nullptr;
    readyFormat_ = LV_COLOR_FORMAT_UNKNOWN;
    ready_ = false;
    xSemaphoreGive(mutex_);
    return true;
}

bool ThumbnailLoader::pickThumbnailPath(const char *gcodePath, char *thumbPath, size_t thumbPathLen) {
    String query = "/server/files/thumbnails?filename=";
    urlEncodeAppend(gcodePath, query);

    String response;
    if (http_.getString(query.c_str(), response, 8000) && parseThumbnailList(response, thumbPath, thumbPathLen)) {
        return true;
    }

    String metaQuery = "/server/files/metadata?filename=";
    urlEncodeAppend(gcodePath, metaQuery);
    response = "";
    if (!http_.getString(metaQuery.c_str(), response, 8000)) return false;

    JsonDocument metaDoc;
    if (deserializeJson(metaDoc, response) != DeserializationError::Ok) return false;

    JsonObject result = metaDoc["result"].as<JsonObject>();
    if (result.isNull()) return false;

    JsonArray thumbs = result["thumbnails"].as<JsonArray>();
    if (thumbs.isNull()) return false;

    char relPath[128] = {};
    if (!pickBestThumbnail(thumbs, relPath, sizeof(relPath))) return false;
    resolveMetadataThumbPath(gcodePath, relPath, thumbPath, thumbPathLen);
    return thumbPath[0] != '\0';
}

bool ThumbnailLoader::fetchThumbnail(const char *gcodePath, void *&pixels, lv_color_format_t &format,
                                       int &w, int &h) {
    pixels = nullptr;
    format = LV_COLOR_FORMAT_UNKNOWN;
    w = h = 0;
    if (!buffer_ || !gcodePath || !gcodePath[0]) return false;

    char thumbPath[160] = {};
    if (!pickThumbnailPath(gcodePath, thumbPath, sizeof(thumbPath))) {
        Serial.printf("[Thumb] no thumbnail metadata for %s\n", gcodePath);
        return false;
    }

    String downloadPath = "/server/files/gcodes/";
    urlEncodeAppend(thumbPath, downloadPath);

    const int len = http_.download(downloadPath.c_str(), buffer_, kBufferSize, 8000);
    if (len <= 0) {
        Serial.printf("[Thumb] download failed http=%d path=%s\n", http_.statusCode(), thumbPath);
        return false;
    }

    uint16_t *rgb565 = nullptr;
    uint8_t *rgb888 = nullptr;
    const bool isPng = len >= 8 && buffer_[0] == 0x89 && buffer_[1] == 'P';
    const bool isJpeg = len >= 2 && buffer_[0] == 0xFF && buffer_[1] == 0xD8;

    if (isPng && ImageDecoder::decodePng(buffer_, static_cast<size_t>(len), rgb888, w, h)) {
        pixels = rgb888;
        format = LV_COLOR_FORMAT_RGB888;
        return true;
    }
    if (isJpeg && ImageDecoder::decodeJpeg(buffer_, static_cast<size_t>(len), rgb565, w, h)) {
        pixels = rgb565;
        format = LV_COLOR_FORMAT_RGB565;
        return true;
    }
    if (!isPng && !isJpeg && ImageDecoder::decodeJpeg(buffer_, static_cast<size_t>(len), rgb565, w, h)) {
        pixels = rgb565;
        format = LV_COLOR_FORMAT_RGB565;
        return true;
    }
    if (!isPng && !isJpeg && ImageDecoder::decodePng(buffer_, static_cast<size_t>(len), rgb888, w, h)) {
        pixels = rgb888;
        format = LV_COLOR_FORMAT_RGB888;
        return true;
    }

    Serial.printf("[Thumb] decode failed for %s (%d bytes)\n", thumbPath, len);
    return false;
}

void ThumbnailLoader::worker(void *arg) {
    auto *self = static_cast<ThumbnailLoader *>(arg);

    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));

        char path[128] = {};
        xSemaphoreTake(self->mutex_, portMAX_DELAY);
        if (!self->hasPending_) {
            xSemaphoreGive(self->mutex_);
            continue;
        }
        strlcpy(path, self->pendingPath_, sizeof(path));
        self->hasPending_ = false;
        self->busy_ = true;
        if (self->readyPixels_) {
            ImageDecoder::freeBuffer(self->readyPixels_);
            self->readyPixels_ = nullptr;
        }
        self->ready_ = false;
        xSemaphoreGive(self->mutex_);

        uint16_t *pixels = nullptr;
        int w = 0;
        int h = 0;
        void *readyPixels = nullptr;
        lv_color_format_t readyFormat = LV_COLOR_FORMAT_UNKNOWN;
        const bool ok = self->fetchThumbnail(path, readyPixels, readyFormat, w, h);

        xSemaphoreTake(self->mutex_, portMAX_DELAY);
        self->busy_ = false;
        if (ok && readyPixels) {
            self->readyPixels_ = readyPixels;
            self->readyFormat_ = readyFormat;
            self->readyW_ = w;
            self->readyH_ = h;
            strlcpy(self->readyPath_, path, sizeof(self->readyPath_));
            self->ready_ = true;
            Serial.printf("[Thumb] ready %s %dx%d\n", path, w, h);
        }
        xSemaphoreGive(self->mutex_);
    }
}
