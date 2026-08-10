#include "ui/Keyboard.h"

lv_obj_t *PaxxKeyboard::kb_ = nullptr;
lv_obj_t *PaxxKeyboard::activeTa_ = nullptr;
PaxxKeyboard::VisibilityFn PaxxKeyboard::visibilityFn_ = nullptr;
void *PaxxKeyboard::visibilityUserData_ = nullptr;

void PaxxKeyboard::setVisibilityListener(VisibilityFn fn, void *userData) {
    visibilityFn_ = fn;
    visibilityUserData_ = userData;
}

void PaxxKeyboard::notifyVisibility(bool visible) {
    if (visibilityFn_) visibilityFn_(visible, visibilityUserData_);
}

void PaxxKeyboard::init(lv_obj_t *parent) {
    if (kb_ || !parent) return;

    kb_ = lv_keyboard_create(parent);
    lv_obj_set_size(kb_, LV_PCT(100), LV_PCT(36));
    lv_obj_align(kb_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(kb_, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_mode(kb_, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_popovers(kb_, false);
    lv_obj_add_event_cb(kb_, keyboardEvent, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb_, keyboardEvent, LV_EVENT_CANCEL, NULL);
}

void PaxxKeyboard::scrollFieldIntoView(lv_obj_t *textarea) {
    if (!textarea) return;

    lv_obj_t *parent = lv_obj_get_parent(textarea);
    while (parent) {
        if (lv_obj_has_flag(parent, LV_OBJ_FLAG_SCROLLABLE)) {
            lv_obj_scroll_to_view(textarea, LV_ANIM_OFF);
            return;
        }
        parent = lv_obj_get_parent(parent);
    }
}

void PaxxKeyboard::asyncScrollCb(void *userData) {
    scrollFieldIntoView(static_cast<lv_obj_t *>(userData));
}

void PaxxKeyboard::showFor(lv_obj_t *textarea) {
    if (!kb_ || !textarea) return;

    const bool alreadyVisible = isVisible() && activeTa_ == textarea;
    activeTa_ = textarea;

    if (!alreadyVisible) {
        lv_keyboard_set_textarea(kb_, textarea);
        lv_obj_remove_flag(kb_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(kb_);
        notifyVisibility(true);
    }

    lv_async_call(asyncScrollCb, textarea);
}

void PaxxKeyboard::hide() {
    if (!kb_ || lv_obj_has_flag(kb_, LV_OBJ_FLAG_HIDDEN)) return;

    lv_async_call_cancel(asyncScrollCb, activeTa_);
    lv_keyboard_set_textarea(kb_, NULL);
    lv_obj_add_flag(kb_, LV_OBJ_FLAG_HIDDEN);
    activeTa_ = nullptr;
    notifyVisibility(false);
}

bool PaxxKeyboard::isVisible() {
    return kb_ && !lv_obj_has_flag(kb_, LV_OBJ_FLAG_HIDDEN);
}

void PaxxKeyboard::promptFor(lv_obj_t *textarea) {
    showFor(textarea);
}

void PaxxKeyboard::textareaEvent(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = static_cast<lv_obj_t *>(lv_event_get_target(e));

    if (code == LV_EVENT_FOCUSED) {
        showFor(ta);
    } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        hide();
    }
}

void PaxxKeyboard::keyboardEvent(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        hide();
    }
}

void PaxxKeyboard::attach(lv_obj_t *textarea, bool password) {
    if (!textarea) return;

    lv_textarea_set_one_line(textarea, true);
    lv_textarea_set_max_length(textarea, 64);
    if (password) lv_textarea_set_password_mode(textarea, true);

    lv_obj_add_event_cb(textarea, textareaEvent, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(textarea, textareaEvent, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(textarea, textareaEvent, LV_EVENT_CANCEL, NULL);
}
