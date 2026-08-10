#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lvgl.h>
#include "net/HttpClient.h"

class ThumbnailLoader {
public:
    void begin(const char *host, uint16_t port, bool useAuth, const char *user, const char *pass,
               const char *apiKey = nullptr);
    void request(const char *gcodePath);
    void clear();
    bool poll(void *&pixels, lv_color_format_t &format, int &w, int &h, char *pathOut, size_t pathOutLen);
    bool isBusy() const { return busy_; }

private:
    static void worker(void *arg);
    bool fetchThumbnail(const char *gcodePath, void *&pixels, lv_color_format_t &format, int &w, int &h);
    bool pickThumbnailPath(const char *gcodePath, char *thumbPath, size_t thumbPathLen);

    HttpClient http_;
    TaskHandle_t workerTask_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;

    volatile bool busy_ = false;
    volatile bool ready_ = false;
    volatile bool hasPending_ = false;

    char pendingPath_[128] = {};
    char readyPath_[128] = {};
    void *readyPixels_ = nullptr;
    lv_color_format_t readyFormat_ = LV_COLOR_FORMAT_UNKNOWN;
    int readyW_ = 0;
    int readyH_ = 0;

    static constexpr size_t kBufferSize = 128 * 1024;
    uint8_t *buffer_ = nullptr;
};
