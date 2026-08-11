#include "ui/Theme.h"

void paxx_disable_input(lv_obj_t *obj) {
    if (!obj) return;
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void PaxxTheme::apply(bool dark) {
    lv_theme_t *theme = lv_theme_default_init(
        lv_display_get_default(),
        primary(), lv_color_hex(0xFFFFFF), dark,
        LV_FONT_DEFAULT);
    lv_display_set_theme(lv_display_get_default(), theme);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, bg(dark), LV_PART_MAIN);
    lv_obj_set_style_text_color(scr, text(dark), LV_PART_MAIN);
}

lv_obj_t *paxx_create_nav_bar(lv_obj_t *parent, const char *title, lv_event_cb_t backCb, void *userData, bool dark,
                              lv_obj_t **outBackBtn) {
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), 48);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, PaxxTheme::surface(dark), LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bar, 8, LV_PART_MAIN);
    paxx_disable_input(bar);

    if (backCb) {
        lv_obj_t *back = lv_btn_create(bar);
        lv_obj_set_size(back, 72, 32);
        lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_add_event_cb(back, backCb, LV_EVENT_CLICKED, userData);
        lv_obj_t *lbl = lv_label_create(back);
        lv_label_set_text(lbl, LV_SYMBOL_LEFT " Back");
        lv_obj_center(lbl);
        if (outBackBtn) *outBackBtn = back;
    } else if (outBackBtn) {
        *outBackBtn = nullptr;
    }

    lv_obj_t *ttl = lv_label_create(bar);
    lv_label_set_text(ttl, title);
    lv_obj_align(ttl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(ttl, &lv_font_montserrat_16, LV_PART_MAIN);
    return bar;
}

lv_obj_t *paxx_create_status_chip(lv_obj_t *parent, const char *label, lv_color_t color) {
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(chip, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(chip, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_radius(chip, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(chip, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(chip, 4, LV_PART_MAIN);
    paxx_disable_input(chip);
    lv_obj_t *lbl = lv_label_create(chip);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, color, LV_PART_MAIN);
    return chip;
}
