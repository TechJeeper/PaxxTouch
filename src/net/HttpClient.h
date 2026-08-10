#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>

class HttpClient {
public:
    void configure(const char *host, uint16_t port = 80);
    void setAuth(bool useAuth, const char *user, const char *pass);
    void setToken(const char *token);
    void setApiKey(const char *apiKey);

    int download(const char *path, uint8_t *buffer, size_t bufferSize, int timeoutMs = 8000,
                 volatile bool *abortRequested = nullptr);
    bool getString(const char *path, String &out, int timeoutMs = 8000);
    bool postJson(const char *path, const char *jsonBody, String &out, int timeoutMs = 8000);
    bool postEmpty(const char *path, int timeoutMs = 3000);
    bool postTouchFast(WiFiClient &client, const char *path);
    int statusCode() const { return lastCode_; }

private:
    void applyAuth(HTTPClient &http);
    void writeAuthHeaders(Client &client);

    char host_[64] = {};
    uint16_t port_ = 80;
    bool useAuth_ = false;
    char user_[32] = {};
    char pass_[64] = {};
    char token_[256] = {};
    char apiKey_[64] = {};
    int lastCode_ = 0;
};
