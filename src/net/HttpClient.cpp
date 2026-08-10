#include "net/HttpClient.h"

#include <lvgl.h>
#include <WiFiClient.h>
#include <base64.h>

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

void HttpClient::postTouchFast(WiFiClient &client, const char *path) {
    if (!host_[0] || !path) return;

    client.setTimeout(150);
    if (!client.connected()) {
        client.stop();
        if (!client.connect(host_, port_)) return;
    }

    client.print(String("POST ") + path + " HTTP/1.1\r\n");
    client.print(String("Host: ") + host_ + "\r\n");
    writeAuthHeaders(client);
    client.print("Content-Length: 0\r\nConnection: keep-alive\r\n\r\n");
    client.flush();

    const uint32_t deadline = millis() + 60;
    while (static_cast<int32_t>(deadline - millis()) > 0) {
        bool readAny = false;
        while (client.available()) {
            client.read();
            readAny = true;
        }
        if (!readAny) delay(1);
        if (!readAny && !client.connected()) break;
    }
}

int HttpClient::download(const char *path, uint8_t *buffer, size_t bufferSize, int timeoutMs,
                         volatile bool *abortRequested) {
    if (!host_[0] || !buffer || bufferSize == 0) return -1;

    HTTPClient http;
    WiFiClient client;
    String url = String("http://") + host_ + ":" + port_ + path;
    http.begin(client, url);
    http.setTimeout(timeoutMs);
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
            lv_task_handler();
            delay(1);
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
