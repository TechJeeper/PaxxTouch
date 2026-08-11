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



/**

 * Mirrors U1 /screen/ using the same protocol as the web client:

 * - GET /screen/snapshot + If-None-Match (304 when idle, decode only on 200)

 * - POST /screen/touch fire-and-forget (never blocks snapshot polling)

 *

 * Pipeline: poll task (HTTP) → decode task (PNG→RGB565) → UI pollFrame()

 */

class RemoteScreenClient {

public:

    static constexpr int U1_WIDTH = 480;

    static constexpr int U1_HEIGHT = 320;



    void begin(const char *host, bool useAuth, const char *user, const char *pass,

               const char *apiKey = nullptr, const char *token = nullptr);

    void setEnabled(bool enabled) { enabled_ = enabled; }

    bool isEnabled() const { return enabled_; }

    void setViewActive(bool active);

    bool isViewActive() const { return viewActive_; }



    void queueTouch(int u1X, int u1Y, RemoteTouchAction action);

    void setRefreshIntervalMs(unsigned long ms) { refreshIntervalMs_ = ms; }

    void pumpSnapshot();
    /** Skip poll interval and fetch on next loop iteration (e.g. after touch). */
    void requestSnapshotNow() { pumpSnapshot(); }

    bool pollFrame(uint8_t *&pixels, lv_color_format_t &format, int &w, int &h);



    void resetProbe();

    void forceProbeFailed(const char *message);

    RemoteProbeState probeState() const { return probeState_; }

    const char *probeError() const { return probeError_; }

    const char *lastSnapshotError() const { return snapshotError_; }

    int lastHttpCode() const { return http_.statusCode(); }



private:

    struct TouchPoint {

        int16_t x;

        int16_t y;

        RemoteTouchAction action;

    };



    void ensureWorkers();

    void applyHttpEndpoint(uint16_t port, const char *snapPath, const char *touchPrefix);

    bool probeEndpoint(uint16_t port, const char *snapPath, const char *touchPrefix, int &outLen);
    bool probeAvailable();

    SnapshotFetchStatus pollSnapshot(int &outLen);

    void signalDecode(int len);

    bool decodeFromFetchBuffer(int len);
    bool decodeFromDecodeBuffer(int len);

    void publishDisplayFrame();

    static void pollWorker(void *arg);

    static void decodeWorker(void *arg);

    static void touchWorker(void *arg);

    static void probeWorker(void *arg);

    static void sendTouchHttp(RemoteScreenClient *self, const TouchPoint &pt);



    char host_[64] = {};

    bool useAuth_ = false;

    char user_[32] = {};

    char pass_[64] = {};

    char apiKey_[64] = {};

    char token_[256] = {};

    bool enabled_ = true;

    volatile bool viewActive_ = false;

    unsigned long refreshIntervalMs_ = 100;



    char snapshotPath_[64] = "/screen/snapshot";

    char touchPathPrefix_[48] = "/screen/touch";

    char snapshotEtag_[32] = {};

    uint32_t notModifiedPolls_ = 0;

    uint32_t snapshotPolls_ = 0;



    volatile RemoteProbeState probeState_ = RemoteProbeState::Idle;

    char probeError_[96] = {};

    char snapshotError_[96] = {};

    unsigned long probeStartedMs_ = 0;

    int lastProbeSnapshotLen_ = 0;



    HttpClient http_;

    HttpClient probeHttp_;

    HttpClient touchHttp_;

    WiFiClient snapshotClient_;

    WiFiClient touchClient_;



    QueueHandle_t touchQueue_ = nullptr;

    TaskHandle_t pollTask_ = nullptr;

    TaskHandle_t decodeTask_ = nullptr;

    TaskHandle_t probeTask_ = nullptr;



    SemaphoreHandle_t fetchMutex_ = nullptr;

    SemaphoreHandle_t frameMutex_ = nullptr;



    static constexpr size_t kBufferSize = 256 * 1024;

    uint8_t *fetchBuf_ = nullptr;

    uint8_t *decodeBuf_ = nullptr;

    uint16_t *displayBuf_ = nullptr;

    volatile int pendingDecodeLen_ = 0;

    volatile bool frameDirty_ = false;

    int displayW_ = 0;

    int displayH_ = 0;

    lv_color_format_t displayFormat_ = LV_COLOR_FORMAT_RGB565;

};


