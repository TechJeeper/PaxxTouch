#include "net/HttpClient.h"

#include <WiFiClient.h>
#include <base64.h>

namespace {

bool readHttpLine(WiFiClient &client, char *line, size_t lineLen, uint32_t deadlineMs, volatile bool *abort) {
    if (!line || lineLen == 0) return false;
    size_t idx = 0;
    while (idx < lineLen - 1 && static_cast<int32_t>(deadlineMs - millis()) > 0) {
        if (abort && *abort) return false;
        while (client.available()) {
            const char c = static_cast<char>(client.read());
            if (c == '\n') {
                line[idx] = '\0';
                return true;
            }
            if (c != '\r' && idx < lineLen - 1) line[idx++] = c;
        }
        if (!client.connected() && !client.available()) break;
        delay(1);
    }
    line[idx] = '\0';
    return idx > 0;
}

void storeEtagHeader(const char *headerValue, char *outEtag, size_t outEtagLen) {
    if (!outEtag || outEtagLen == 0) return;
    outEtag[0] = '\0';
    if (!headerValue || !headerValue[0]) return;

    const char *p = headerValue;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == '"') ++p;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < outEtagLen) {
        outEtag[i++] = *p++;
    }
    outEtag[i] = '\0';
}

int parseContentLength(const char *line) {
    if (!line) return -1;
    const char *p = strstr(line, "Content-Length:");
    if (!p) p = strstr(line, "content-length:");
    if (!p) return -1;
    p = strchr(p, ':');
    if (!p) return -1;
    return atoi(p + 1);
}

bool headerIndicatesClose(const char *line) {
    if (!line) return false;
    const char *p = line;
    while (*p == ' ' || *p == '\t') ++p;
    if (strncasecmp(p, "Connection:", 11) != 0) return false;
    p = strchr(p, ':');
    if (!p) return false;
    ++p;
    while (*p == ' ' || *p == '\t') ++p;
    return strncasecmp(p, "close", 5) == 0;
}

void drainClient(WiFiClient &client, uint32_t deadlineMs, volatile bool *abort) {
    while (static_cast<int32_t>(deadlineMs - millis()) > 0 && (client.connected() || client.available())) {
        if (abort && *abort) break;
        while (client.available()) client.read();
        if (!client.available()) delay(1);
    }
}

}  // namespace

void HttpClient::configure(const char *host, uint16_t port) {
    strlcpy(host_, host, sizeof(host_));
    port_ = port;
}

void HttpClient::setAuth(bool useAuth, const char *user, const char *pass) {
    useAuth_ = useAuth;
    if (user) strlcpy(user_, user, sizeof(user_));
    if (pass) strlcpy(pass_, pass, sizeof(pass_));
}

void HttpClient::setToken(const char *token) {
    if (token) strlcpy(token_, token, sizeof(token_));
    else token_[0] = '\0';
}

void HttpClient::setApiKey(const char *apiKey) {
    if (apiKey) strlcpy(apiKey_, apiKey, sizeof(apiKey_));
    else apiKey_[0] = '\0';
}

void HttpClient::applyAuth(HTTPClient &http) {
    if (apiKey_[0] != '\0') {
        http.addHeader("X-Api-Key", apiKey_);
    } else if (token_[0] != '\0') {
        http.addHeader("Authorization", String("Bearer ") + token_);
    } else if (useAuth_) {
        http.setAuthorization(user_, pass_);
    }
}

void HttpClient::writeAuthHeaders(Client &client) {
    if (apiKey_[0] != '\0') {
        client.print("X-Api-Key: ");
        client.print(apiKey_);
        client.print("\r\n");
    } else if (token_[0] != '\0') {
        client.print("Authorization: Bearer ");
        client.print(token_);
        client.print("\r\n");
    } else if (useAuth_) {
        const String creds = String(user_) + ":" + pass_;
        client.print("Authorization: Basic ");
        client.print(base64::encode(creds));
        client.print("\r\n");
    }
}

bool HttpClient::postTouchFireAndForget(WiFiClient &client, const char *path) {
    return postTouchSend(client, path, false, true);
}

bool HttpClient::postTouchSend(WiFiClient &client, const char *path, bool waitResponse, bool reuseConnection) {
    if (!host_[0] || !path) return false;

    auto closeClient = [&]() { client.stop(); };

    if (!reuseConnection || !client.connected()) {
        client.stop();
        client.setTimeout(300);
        if (!client.connect(host_, port_, 500)) {
            Serial.println("[Remote] touch connect failed");
            return false;
        }
    } else {
        client.setTimeout(300);
    }

    client.print(String("POST ") + path + " HTTP/1.1\r\n");
    client.print(String("Host: ") + host_ + "\r\n");
    writeAuthHeaders(client);
    client.print("Content-Length: 0\r\n");
    if (reuseConnection) {
        client.print("Connection: keep-alive\r\n\r\n");
    } else {
        client.print("Connection: close\r\n\r\n");
    }
    client.flush();

    const uint32_t deadline = millis() + (waitResponse ? 300u : 80u);
    char statusLine[32] = {};
    size_t idx = 0;
    while (idx < sizeof(statusLine) - 1 && static_cast<int32_t>(deadline - millis()) > 0) {
        while (client.available()) {
            const char c = static_cast<char>(client.read());
            if (c == '\n') goto headers;
            if (c != '\r') statusLine[idx++] = c;
        }
        if (!client.connected() && !client.available()) break;
        delay(1);
    }
headers:
    int httpCode = 0;
    if (strncmp(statusLine, "HTTP/1.", 7) == 0) {
        const char *sp = strchr(statusLine + 7, ' ');
        if (sp) httpCode = atoi(sp + 1);
    }
    lastCode_ = httpCode;

    bool serverClose = !reuseConnection;
    char line[64] = {};
    while (readHttpLine(client, line, sizeof(line), deadline, nullptr)) {
        if (line[0] == '\0') break;
        if (headerIndicatesClose(line)) serverClose = true;
    }

    drainClient(client, deadline, nullptr);
    if (serverClose || !reuseConnection) closeClient();

    if (httpCode != 200 && httpCode != 0) {
        Serial.printf("[Remote] touch HTTP %d (%s)\n", httpCode, statusLine[0] ? statusLine : "no response");
        return false;
    }
    return true;
}

bool HttpClient::postTouchFast(WiFiClient &client, const char *path) {
    return postTouchSend(client, path, true);
}

SnapshotFetchStatus HttpClient::fetchSnapshot(const char *path, uint8_t *buffer, size_t bufferSize,
                                              int timeoutMs, volatile bool *abortRequested,
                                              WiFiClient &client, const char *ifNoneMatch,
                                              char *outEtag, size_t outEtagLen, int &outLen,
                                              bool reuseConnection) {
    outLen = 0;
    lastCode_ = 0;
    if (!host_[0] || !path || !buffer || bufferSize == 0) return SnapshotFetchStatus::Error;

    auto closeClient = [&]() { client.stop(); };

    if (!reuseConnection || !client.connected()) {
        client.stop();
        client.setTimeout(timeoutMs);
        if (!client.connect(host_, port_, static_cast<uint16_t>(min(timeoutMs, 2000)))) {
            return SnapshotFetchStatus::Error;
        }
    } else {
        client.setTimeout(timeoutMs);
    }

    client.print(String("GET ") + path + " HTTP/1.1\r\n");
    client.print(String("Host: ") + host_ + "\r\n");
    writeAuthHeaders(client);
    if (ifNoneMatch && ifNoneMatch[0]) {
        client.print("If-None-Match: \"");
        client.print(ifNoneMatch);
        client.print("\"\r\n");
    }
    if (reuseConnection) {
        client.print("Connection: keep-alive\r\n\r\n");
    } else {
        client.print("Connection: close\r\n\r\n");
    }
    client.flush();
    const uint32_t deadline = millis() + static_cast<uint32_t>(timeoutMs);
    char line[128] = {};
    if (!readHttpLine(client, line, sizeof(line), deadline, abortRequested)) {
        client.stop();
        return SnapshotFetchStatus::Error;
    }

    int httpCode = 0;
    if (strncmp(line, "HTTP/1.", 7) == 0) {
        const char *sp = strchr(line + 7, ' ');
        if (sp) httpCode = atoi(sp + 1);
    }
    lastCode_ = httpCode;

    char responseEtag[32] = {};
    int contentLength = -1;
    bool serverClose = !reuseConnection;
    while (readHttpLine(client, line, sizeof(line), deadline, abortRequested)) {
        if (line[0] == '\0') break;
        if (strncasecmp(line, "ETag:", 5) == 0) {
            storeEtagHeader(line + 5, responseEtag, sizeof(responseEtag));
        } else if (headerIndicatesClose(line)) {
            serverClose = true;
        } else {
            const int cl = parseContentLength(line);
            if (cl >= 0) contentLength = cl;
        }
    }

    if (abortRequested && *abortRequested) {
        closeClient();
        return SnapshotFetchStatus::Error;
    }

    if (httpCode == 304 || httpCode == 204) {
        if (responseEtag[0] && outEtag && outEtagLen > 0) {
            strlcpy(outEtag, responseEtag, outEtagLen);
        }
        drainClient(client, deadline, abortRequested);
        if (serverClose || !reuseConnection) closeClient();
        return SnapshotFetchStatus::NotModified;
    }

    if (httpCode != 200) {
        closeClient();
        return SnapshotFetchStatus::Error;
    }

    size_t total = 0;
    if (contentLength > 0 && static_cast<size_t>(contentLength) <= bufferSize) {
        while (total < static_cast<size_t>(contentLength) && static_cast<int32_t>(deadline - millis()) > 0) {
            if (abortRequested && *abortRequested) {
                closeClient();
                return SnapshotFetchStatus::Error;
            }
            if (!client.available()) {
                if (!client.connected()) break;
                delay(1);
                continue;
            }
            const size_t avail = client.available();
            total += client.readBytes(buffer + total, min(avail, bufferSize - total));
        }
    } else {
        while (client.connected() && total < bufferSize && static_cast<int32_t>(deadline - millis()) > 0) {
            if (abortRequested && *abortRequested) {
                closeClient();
                return SnapshotFetchStatus::Error;
            }
            if (!client.available()) {
                delay(1);
                continue;
            }
            const size_t avail = client.available();
            total += client.readBytes(buffer + total, min(avail, bufferSize - total));
        }
    }

    drainClient(client, deadline, abortRequested);
    if (serverClose || !reuseConnection) closeClient();

    if (total == 0) return SnapshotFetchStatus::Error;

    if (responseEtag[0] && outEtag && outEtagLen > 0) {
        strlcpy(outEtag, responseEtag, outEtagLen);
    }
    outLen = static_cast<int>(total);
    return SnapshotFetchStatus::Ok;
}

int HttpClient::download(const char *path, uint8_t *buffer, size_t bufferSize, int timeoutMs,
                         volatile bool *abortRequested, WiFiClient *client) {
    if (!host_[0] || !buffer || bufferSize == 0) return -1;

    HTTPClient http;
    WiFiClient localClient;
    WiFiClient &netClient = client ? *client : localClient;
    String url = String("http://") + host_ + ":" + port_ + path;
    http.begin(netClient, url);
    http.setTimeout(timeoutMs);
    http.addHeader("Cache-Control", "no-cache");
    http.addHeader("Pragma", "no-cache");
    applyAuth(http);

    lastCode_ = http.GET();
    if (lastCode_ != HTTP_CODE_OK) {
        http.end();
        return -1;
    }

    WiFiClient *stream = http.getStreamPtr();
    size_t total = 0;
    const uint32_t deadline = millis() + static_cast<uint32_t>(timeoutMs);
    while (http.connected() && total < bufferSize && static_cast<int32_t>(deadline - millis()) > 0) {
        if (abortRequested && *abortRequested) {
            http.end();
            return -1;
        }
        const size_t avail = stream->available();
        if (avail == 0) {
            if (!stream->connected()) break;
            vTaskDelay(1);
            continue;
        }
        total += stream->readBytes(buffer + total, min(avail, bufferSize - total));
    }
    http.end();
    return static_cast<int>(total);
}

bool HttpClient::getString(const char *path, String &out, int timeoutMs) {
    if (!host_[0]) return false;
    HTTPClient http;
    WiFiClient client;
    String url = String("http://") + host_ + ":" + port_ + path;
    http.begin(client, url);
    http.setTimeout(timeoutMs);
    applyAuth(http);
    lastCode_ = http.GET();
    if (lastCode_ != HTTP_CODE_OK) {
        http.end();
        return false;
    }
    out = http.getString();
    http.end();
    return true;
}

bool HttpClient::postEmpty(const char *path, int timeoutMs) {
    if (!host_[0]) return false;
    HTTPClient http;
    WiFiClient client;
    String url = String("http://") + host_ + ":" + port_ + path;
    http.begin(client, url);
    http.setTimeout(timeoutMs);
    applyAuth(http);
    lastCode_ = http.POST("");
    http.end();
    return lastCode_ == HTTP_CODE_OK || lastCode_ == HTTP_CODE_NO_CONTENT;
}

bool HttpClient::postJson(const char *path, const char *jsonBody, String &out, int timeoutMs) {
    if (!host_[0]) return false;
    HTTPClient http;
    WiFiClient client;
    String url = String("http://") + host_ + ":" + port_ + path;
    http.begin(client, url);
    http.setTimeout(timeoutMs);
    http.addHeader("Content-Type", "application/json");
    applyAuth(http);
    lastCode_ = http.POST(jsonBody);
    if (lastCode_ != HTTP_CODE_OK && lastCode_ != HTTP_CODE_CREATED) {
        http.end();
        return false;
    }
    out = http.getString();
    http.end();
    return true;
}
