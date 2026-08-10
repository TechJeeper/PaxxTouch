#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <functional>
#include <vector>

struct WifiNetwork {
    char ssid[33];
    int32_t rssi;
    bool secure;
};

using WifiStatusCallback = std::function<void(bool connected, const char *message)>;

class WifiService {
public:
    bool connect(const char *ssid, const char *password, int timeoutSec = 20);
    bool startConnect(const char *ssid, const char *password, int timeoutSec = 20);
    bool isConnectPending() const { return connectPending_; }
    void disconnect();
    bool isConnected() const { return WiFi.status() == WL_CONNECTED; }
    const char *localIp() const;
    void scan(std::vector<WifiNetwork> &out);
    void forgetAll();
    void setStatusCallback(WifiStatusCallback cb) { statusCb_ = std::move(cb); }
    void loop();

private:
    void finishConnect(bool ok, const char *message);
    int runScan(bool showHidden = true);

    WifiStatusCallback statusCb_;
    bool lastConnected_ = false;
    bool connectPending_ = false;
    unsigned long connectStartMs_ = 0;
    int connectTimeoutSec_ = 0;
    char lastSsid_[33] = {};
    char lastPass_[64] = {};
};
