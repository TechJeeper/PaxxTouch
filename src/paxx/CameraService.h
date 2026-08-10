#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "net/HttpClient.h"
#include "paxx/ImageDecoder.h"

class CameraService {
public:
    void begin(const char *host, bool useAuth, const char *user, const char *pass, const char *apiKey = nullptr);
    void requestFetch();
    bool poll(uint16_t *&rgb565, int &w, int &h);
    bool fetchSnapshot(uint16_t *&rgb565, int &w, int &h);
    int lastHttpCode() const { return http_.statusCode(); }

private:
    bool fetchSnapshotInternal(uint16_t *&rgb565, int &w, int &h);
    static void worker(void *arg);

    HttpClient http_;
    char host_[64] = {};
    bool useAuth_ = false;
    char user_[32] = {};
    char pass_[64] = {};
    static constexpr size_t kBufferSize = 256 * 1024;
    uint8_t *buffer_ = nullptr;

    SemaphoreHandle_t mutex_ = nullptr;
    TaskHandle_t workerTask_ = nullptr;
    volatile bool fetchPending_ = false;
    uint16_t *readyPixels_ = nullptr;
    int readyW_ = 0;
    int readyH_ = 0;
    bool ready_ = false;
};
