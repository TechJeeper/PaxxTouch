#pragma once

#include <Arduino.h>
#include <functional>

using OtaCallback = std::function<void(const char *message, int progress)>;

class OtaService {
public:
    void begin(const char *hostname = "paxxtouch");
    void loop();
    void setCallback(OtaCallback cb) { cb_ = std::move(cb); }

private:
    bool started_ = false;
    OtaCallback cb_;
};
