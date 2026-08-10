#pragma once

#include <Arduino.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <functional>
#include <vector>

enum class ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Error
};

enum class PrintState {
    Unknown,
    Standby,
    Printing,
    Paused,
    Complete,
    Cancelled,
    Error
};

struct FilamentSlot {
    int index = 0;
    char material[24];
    char color[16];
    float remainingMm = 0;
    bool loaded = false;
    int mappedTool = -1;
};

struct PrinterStatus {
    ConnectionState connection = ConnectionState::Disconnected;
    PrintState printState = PrintState::Unknown;
    char filename[96];
    float progress = 0;
    float printDuration = 0;
    float totalDuration = 0;
    float nozzleTemp = 0;
    float nozzleTarget = 0;
    float bedTemp = 0;
    float bedTarget = 0;
    float extruderTemps[4] = {};
    float extruderTargets[4] = {};
    float speedFactor = 100.0f;
    float flowFactor = 100.0f;
    int activeTool = 0;
    char stateMessage[128];
    std::vector<FilamentSlot> filaments;
};

using StatusCallback = std::function<void(const PrinterStatus &)>;
using NotifyCallback = std::function<void(const char *title, const char *message)>;

class MoonrakerClient {
public:
    void begin(const char *host, uint16_t port, const char *token = nullptr, const char *apiKey = nullptr);
    void loop();
    bool pollViaRest(class MoonrakerRest &rest);

    void setStatusCallback(StatusCallback cb) { statusCb_ = std::move(cb); }
    void setNotifyCallback(NotifyCallback cb) { notifyCb_ = std::move(cb); }
    ConnectionState connectionState() const { return status_.connection; }
    const PrinterStatus &status() const { return status_; }

    void connect();
    void disconnect();
    bool isConnectedTo(const char *host, uint16_t port) const;
    bool isLinkedTo(const char *host, uint16_t port) const;
    bool isRestOnly() const { return restOnly_; }
    void sendGcode(const char *script);
    void pausePrint();
    void resumePrint();
    void cancelPrint();
    void setActiveTool(int toolIndex);
    void setSpeedFactor(int percent);
    void setFlowFactor(int percent);
    void homeAll();
    void bedMesh();
    void refreshObjects();

private:
    void onWebSocketEvent(WStype_t type, uint8_t *payload, size_t length);
    void sendJsonRpc(const char *method, JsonDocument &params, int id);
    void handleMessage(const char *payload, size_t length);
    void parseStatusUpdate(JsonObject &status);
    void parsePrintTaskConfig(JsonObject &cfg);
    void parseExtruder(JsonObject &root, const char *name, int idx);
    void setConnection(ConnectionState state);
    void checkNotifications();
    void identifyConnection();
    void handleRpcResponse(JsonDocument &doc);
    void recomputeProgress();
    void resetProgressCache();

    float cachedDisplayProgress_ = -1.0f;
    float cachedVsdProgress_ = -1.0f;
    float cachedLayerProgress_ = -1.0f;

    WebSocketsClient ws_;
    StatusCallback statusCb_;
    NotifyCallback notifyCb_;
    PrinterStatus status_;
    PrintState lastPrintState_ = PrintState::Unknown;

    char host_[64] = {};
    uint16_t port_ = 7125;
    char token_[256] = {};
    char apiKey_[64] = {};
    bool restOnly_ = false;
    int rpcId_ = 1;
    int identifyRpcId_ = 0;
    bool identified_ = false;
    bool subscribeSent_ = false;
    unsigned long identifySentMs_ = 0;
    unsigned long connectStartMs_ = 0;
};
