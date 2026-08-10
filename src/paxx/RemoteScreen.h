#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <WiFiClient.h>
#include "net/HttpClient.h"
#include "paxx/ImageDecoder.h"

enum class RemoteTouchAction : uint8_t { Down, Move, Up };

enum class RemoteProbeState : uint8_t { Idle, Running, Ok, Failed };

class RemoteScreenClient {
public:
    static constexpr int U1_WIDTH = 480;
    static constexpr int U1_HEIGHT = 320;

    void begin(const char *host, bool useAuth, const char *user, const char *pass, const char *apiKey = nullptr);
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

    void queueTouch(int u1X, int u1Y, RemoteTouchAction action);
    void setRefreshIntervalMs(unsigned long ms) { refreshIntervalMs_ = ms; }
    void pumpSnapshot();
    bool pollFrame(uint8_t *&pixels, lv_color_format_t &format, int &w, int &h);

    void resetProbe();
    RemoteProbeState probeState() const { return probeState_; }
    const char *probeError() const { return probeError_; }

    bool probeAvailable();
    bool shouldDeferSnapshot() const;
    int lastHttpCode() const { return http_.statusCode(); }

private:
    struct TouchPoint {
        int16_t x;
        int16_t y;
        RemoteTouchAction action;
    };

    bool downloadSnapshot(int &outLen, bool forProbe = false);
    bool decodeSnapshot(int len, uint8_t *&pixels, lv_color_format_t &format, int &w, int &h);
    void publishFrame(uint8_t *pixels, lv_color_format_t format, int w, int h);
    void discardPendingFrame();
    static void touchWorker(void *arg);
    static void snapshotWorker(void *arg);
    static void probeWorker(void *arg);
    static void sendTouchEvent(RemoteScreenClient *self, const TouchPoint &pt);
    static void flushTouchBatch(RemoteScreenClient *self, TouchPoint &down, TouchPoint &move, TouchPoint &up,
                                bool &hasDown, bool &hasMove, bool &hasUp);

    char host_[64] = {};
    bool useAuth_ = false;
    char user_[32] = {};
    char pass_[64] = {};
    bool enabled_ = true;
    unsigned long lastTouchActivityMs_ = 0;
    unsigned long refreshIntervalMs_ = 1200;
    unsigned long lastSnapshotMs_ = 0;

    volatile bool abortSnapshot_ = false;
    volatile bool snapshotBusy_ = false;
    volatile RemoteProbeState probeState_ = RemoteProbeState::Idle;
    char probeError_[96] = {};

    HttpClient http_;
    HttpClient probeHttp_;
    HttpClient touchHttp_;
    WiFiClient touchClient_;
    SemaphoreHandle_t httpMutex_ = nullptr;
    QueueHandle_t touchQueue_ = nullptr;
    TaskHandle_t snapshotTask_ = nullptr;
    TaskHandle_t probeTask_ = nullptr;

    uint8_t *pendingPixels_ = nullptr;
    lv_color_format_t pendingFormat_ = LV_COLOR_FORMAT_UNKNOWN;
    int pendingW_ = 0;
    int pendingH_ = 0;
    bool pendingReady_ = false;
    SemaphoreHandle_t frameMutex_ = nullptr;

    static constexpr size_t kBufferSize = 256 * 1024;
    uint8_t *buffer_ = nullptr;
};
