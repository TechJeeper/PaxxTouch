#include "ui/Notify.h"

#include <Arduino.h>

lv_obj_t *PaxxNotify::panel_ = nullptr;
lv_obj_t *PaxxNotify::titleLbl_ = nullptr;
lv_obj_t *PaxxNotify::msgLbl_ = nullptr;
uint32_t PaxxNotify::hideAtMs_ = 0;

void PaxxNotify::init(lv_obj_t *parent) {
    panel_ = lv_obj_create(parent);
    lv_obj_set_size(panel_, LV_PCT(92), LV_SIZE_CONTENT);
    lv_obj_align(panel_, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(panel_, lv_color_hex(0x1F2937), LV_PART_MAIN);
    lv_obj_set_style_border_color(panel_, lv_color_hex(0x10B981), LV_PART_MAIN);
    lv_obj_set_style_border_width(panel_, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel_, 10, LV_PART_MAIN);
    lv_obj_add_flag(panel_, LV_OBJ_FLAG_HIDDEN);

    titleLbl_ = lv_label_create(panel_);
    lv_obj_set_style_text_font(titleLbl_, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_label_set_text(titleLbl_, "");

    msgLbl_ = lv_label_create(panel_);
    lv_obj_align(msgLbl_, LV_ALIGN_TOP_LEFT, 0, 22);
    lv_obj_set_width(msgLbl_, LV_PCT(100));
    lv_label_set_long_mode(msgLbl_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(msgLbl_, "");
}

void PaxxNotify::show(const char *title, const char *message, uint32_t durationMs) {
    if (!panel_) return;
    lv_label_set_text(titleLbl_, title ? title : "");
    lv_label_set_text(msgLbl_, message ? message : "");
    lv_obj_clear_flag(panel_, LV_OBJ_FLAG_HIDDEN);
    hideAtMs_ = millis() + durationMs;
}

void PaxxNotify::loop() {
    if (!panel_ || lv_obj_has_flag(panel_, LV_OBJ_FLAG_HIDDEN)) return;
    if (hideAtMs_ > 0 && static_cast<int32_t>(millis() - hideAtMs_) >= 0) {
        lv_obj_add_flag(panel_, LV_OBJ_FLAG_HIDDEN);
        hideAtMs_ = 0;
    }
}
