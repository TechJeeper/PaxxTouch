#pragma once

#include <Arduino.h>

class ConsoleLog {
public:
    static constexpr size_t kLineCount = 48;
    static constexpr size_t kLineLen = 96;

    static ConsoleLog &instance();

    void append(const char *line);
    void appendf(const char *fmt, ...);
    void getText(char *out, size_t outLen) const;

private:
    ConsoleLog() = default;

    char lines_[kLineCount][kLineLen] = {};
    size_t head_ = 0;
    size_t count_ = 0;
};

void paxx_log(const char *fmt, ...);
