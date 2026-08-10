#pragma once

#include <Arduino.h>
#include "net/HttpClient.h"
#include "paxx/ImageDecoder.h"

class CameraService {
public:
    void begin(const char *host, bool useAuth, const char *user, const char *pass, const char *apiKey = nullptr);
    bool fetchSnapshot(uint16_t *&rgb565, int &w, int &h);
    int lastHttpCode() const { return http_.statusCode(); }

private:
    HttpClient http_;
    char host_[64] = {};
    bool useAuth_ = false;
    char user_[32] = {};
    char pass_[64] = {};
    static constexpr size_t kBufferSize = 256 * 1024;
    uint8_t *buffer_ = nullptr;
};
