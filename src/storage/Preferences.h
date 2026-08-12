#pragma once

#include <Preferences.h>
#include <Arduino.h>

static constexpr int PAXX_MAX_PROFILES = 8;

struct WifiConfig {
    char ssid[33];
    char password[65];
};

struct PrinterProfile {
    char name[24];
    char host[64];
    uint16_t moonrakerPort;
    char apiKey[64];
    bool useAuth;
    char username[32];
    char password[64];
};

struct AppConfig {
    WifiConfig wifi;
    PrinterProfile profiles[PAXX_MAX_PROFILES];
    int profileCount;
    int activeProfile;
    bool remoteScreenEnabled;
    bool darkTheme;
    bool notificationsEnabled;
    float lastSpeedFactor;
    float lastFlowFactor;
};

PrinterProfile &activeProfile(AppConfig &cfg);
const PrinterProfile &activeProfile(const AppConfig &cfg);

class PaxxPreferences {
public:
    static PaxxPreferences &instance();

    void begin();
    void load(AppConfig &out);
    void save(const AppConfig &cfg);
    bool hasWifi() const;
    bool hasPrinter() const;

private:
    PaxxPreferences() = default;
    Preferences prefs_;
    bool loaded_ = false;
};
