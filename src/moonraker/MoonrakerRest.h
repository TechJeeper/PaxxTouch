#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include <vector>

struct MoonrakerFileEntry {
    char path[128];
    char root[32];
    size_t size;
    double modified;
    bool isDir;
};

struct TimelapseEntry {
    char name[96];
    char path[128];
    size_t size;
    double modified;
};

using FileListCallback = std::function<void(bool ok, const std::vector<MoonrakerFileEntry> &files)>;
using TimelapseListCallback = std::function<void(bool ok, const std::vector<TimelapseEntry> &items)>;
using LoginCallback = std::function<void(bool ok, const char *token)>;

class MoonrakerRest {
public:
    void configure(const char *host, uint16_t port, bool useAuth, const char *user, const char *pass, const char *apiKey);

    bool login(String &tokenOut);
    bool pingServer();
    bool queryPrinterStatus(String &statusJsonOut);
    int lastStatusCode() const { return lastStatusCode_; }
    void listFiles(const char *root, const char *path, FileListCallback cb);
    void listTimelapses(TimelapseListCallback cb);
    bool startPrint(const char *root, const char *filename);
    bool sendGcodeScript(const char *script);
    bool setFilamentSlot(int slot, const char *material, const char *color);
    const char *authToken() const { return token_; }

private:
    bool requestJson(const char *method, const char *path, const char *body, String &out);

    char host_[64] = {};
    uint16_t port_ = 7125;
    bool useAuth_ = false;
    char user_[32] = {};
    char pass_[64] = {};
    char apiKey_[64] = {};
    char token_[256] = {};
    int lastStatusCode_ = 0;
};
