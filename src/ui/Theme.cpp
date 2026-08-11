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

void paxx_spinner_anim(void *obj, int32_t v) {
    lv_arc_set_end_angle(static_cast<lv_obj_t *>(obj), static_cast<int>(v));
}

lv_obj_t *paxx_create_loading_arc(lv_obj_t *parent) {
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, 52, 52);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, -24);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_angles(arc, 0, 90);
    lv_obj_set_style_arc_width(arc, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x4DA3FF), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x303030), LV_PART_MAIN);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    paxx_disable_input(arc);
    lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, arc);
    lv_anim_set_exec_cb(&anim, paxx_spinner_anim);
    lv_anim_set_values(&anim, 30, 390);
    lv_anim_set_time(&anim, 900);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&anim);
    return arc;
}

void paxx_set_loading_visible(lv_obj_t *arc, lv_obj_t *label, bool visible, const char *text) {
    if (arc) {
        if (visible) {
            lv_obj_clear_flag(arc, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(arc);
        } else {
            lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (label) {
        const bool showText = visible && text && text[0];
        lv_label_set_text(label, showText ? text : "");
        if (showText) {
            lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(label);
        } else if (!visible) {
            lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
