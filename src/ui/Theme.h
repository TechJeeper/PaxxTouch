#pragma once

#include <lvgl.h>

namespace PaxxTheme {
    inline lv_color_t primary() { return lv_color_hex(0x2563EB); }
    inline lv_color_t accent() { return lv_color_hex(0x10B981); }
    inline lv_color_t warn() { return lv_color_hex(0xF59E0B); }
    inline lv_color_t danger() { return lv_color_hex(0xEF4444); }
    inline lv_color_t bg(bool dark = true) { return lv_color_hex(dark ? 0x111827 : 0xF3F4F6); }
    inline lv_color_t surface(bool dark = true) { return lv_color_hex(dark ? 0x1F2937 : 0xFFFFFF); }
    inline lv_color_t text(bool dark = true) { return lv_color_hex(dark ? 0xF9FAFB : 0x111827); }
    inline lv_color_t muted(bool dark = true) { return lv_color_hex(dark ? 0x9CA3AF : 0x6B7280); }

    void apply(bool dark = true);
}

lv_obj_t *paxx_create_nav_bar(lv_obj_t *parent, const char *title, lv_event_cb_t backCb, void *userData,
                              bool dark = true, lv_obj_t **outBackBtn = nullptr);
lv_obj_t *paxx_create_status_chip(lv_obj_t *parent, const char *label, lv_color_t color);
void paxx_disable_input(lv_obj_t *obj);
void paxx_spinner_anim(void *obj, int32_t v);
lv_obj_t *paxx_create_loading_arc(lv_obj_t *parent);
void paxx_set_loading_visible(lv_obj_t *arc, lv_obj_t *label, bool visible, const char *text = nullptr);
void paxx_style_form_screen(lv_obj_t *screen);
void paxx_set_form_width(lv_obj_t *obj);

constexpr int kPaxxFormWidth = 736;

/** Pump LVGL once so the display updates (call after showing UI, before blocking work). */
void paxx_ui_refresh();
