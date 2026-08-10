#pragma once

#include <lvgl.h>

class PaxxNotify {
public:
    static void init(lv_obj_t *parent);
    static void show(const char *title, const char *message, uint32_t durationMs = 4000);
    static void loop();

private:
    static lv_obj_t *panel_;
    static lv_obj_t *titleLbl_;
    static lv_obj_t *msgLbl_;
    static uint32_t hideAtMs_;
};
