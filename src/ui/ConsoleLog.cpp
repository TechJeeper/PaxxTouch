#include "ui/ConsoleLog.h"

#include <stdarg.h>
#include <stdio.h>

ConsoleLog &ConsoleLog::instance() {
    static ConsoleLog log;
    return log;
}

void ConsoleLog::append(const char *line) {
    if (!line) return;
    strlcpy(lines_[head_], line, kLineLen);
    head_ = (head_ + 1) % kLineCount;
    if (count_ < kLineCount) count_++;
}

void ConsoleLog::appendf(const char *fmt, ...) {
    char buf[kLineLen];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    append(buf);
    Serial.println(buf);
}

void ConsoleLog::getText(char *out, size_t outLen) const {
    if (!out || outLen == 0) return;
    out[0] = '\0';

    const size_t start = count_ < kLineCount ? 0 : head_;
    size_t written = 0;
    for (size_t i = 0; i < count_; ++i) {
        const size_t idx = (start + i) % kLineCount;
        const int n = snprintf(out + written, outLen - written, "%s\n", lines_[idx]);
        if (n <= 0 || static_cast<size_t>(n) >= outLen - written) break;
        written += static_cast<size_t>(n);
    }
}

void paxx_log(const char *fmt, ...) {
    char buf[ConsoleLog::kLineLen];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    ConsoleLog::instance().append(buf);
    Serial.println(buf);
}
