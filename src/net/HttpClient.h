#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>

/** Result of a conditional GET /screen/snapshot (matches U1 fb-http-server + web UI). */
enum class SnapshotFetchStatus : int8_t {
    Error = -1,
    NotModified = 0,
    Ok = 1,
};

class HttpClient {
public:
    void configure(const char *host, uint16_t port = 80);
    void setAuth(bool useAuth, const char *user, const char *pass);
    void setToken(const char *token);
    void setApiKey(const char *apiKey);

    int download(const char *path, uint8_t *buffer, size_t bufferSize, int timeoutMs = 8000,
                 volatile bool *abortRequested = nullptr, WiFiClient *client = nullptr);
    bool getString(const char *path, String &out, int timeoutMs = 8000);
    bool postJson(const char *path, const char *jsonBody, String &out, int timeoutMs = 8000);
    bool postEmpty(const char *path, int timeoutMs = 3000);
    bool postTouchFast(WiFiClient &client, const char *path);
    /** Fire-and-forget POST; closes socket without reading response (matches browser fetch().catch). */
    bool postTouchFireAndForget(WiFiClient &client, const char *path);
    /** Fire-and-forget or wait; reuseConnection keeps TCP open for rapid touch batches. */
    bool postTouchSend(WiFiClient &client, const char *path, bool waitResponse = false,
                       bool reuseConnection = false);
    /** GET with optional If-None-Match; handles 200 (body), 304/204 (unchanged).
     *  When reuseConnection is true, keeps the TCP socket open for the next poll (HTTP/1.1 keep-alive). */
    SnapshotFetchStatus fetchSnapshot(const char *path, uint8_t *buffer, size_t bufferSize, int timeoutMs,
                                    volatile bool *abortRequested, WiFiClient &client,
                                    const char *ifNoneMatch, char *outEtag, size_t outEtagLen, int &outLen,
                                    bool reuseConnection = false);
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
