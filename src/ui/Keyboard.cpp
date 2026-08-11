#include "ui/Keyboard.h"

lv_obj_t *PaxxKeyboard::kb_ = nullptr;
lv_obj_t *PaxxKeyboard::activeTa_ = nullptr;
PaxxKeyboard::VisibilityFn PaxxKeyboard::visibilityFn_ = nullptr;
void *PaxxKeyboard::visibilityUserData_ = nullptr;

namespace {

PaxxKbMode modeForTextarea(lv_obj_t *textarea) {
    if (!textarea) return PaxxKbMode::Text;
    const intptr_t raw = reinterpret_cast<intptr_t>(lv_obj_get_user_data(textarea));
    if (raw <= static_cast<intptr_t>(PaxxKbMode::Number)) {
        return static_cast<PaxxKbMode>(raw);
    }
    return PaxxKbMode::Text;
}

void applyKeyboardMode(lv_obj_t *kb, PaxxKbMode mode) {
    if (!kb) return;
    switch (mode) {
        case PaxxKbMode::Number:
            lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);
            break;
        case PaxxKbMode::Password:
        case PaxxKbMode::Text:
        default:
            lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
            break;
    }
}

void initIpKeyboardMap(lv_obj_t *kb) {
    static const char *const ipMap[] = {
        "1", "2", "3", "\n",
        "4", "5", "6", "\n",
        "7", "8", "9", "\n",
        LV_SYMBOL_BACKSPACE, "0", ".", "\n",
        LV_SYMBOL_CLOSE, LV_SYMBOL_OK, "",
    };
    static const lv_buttonmatrix_ctrl_t ipCtrl[] = {
        LV_BUTTONMATRIX_CTRL_WIDTH_4, LV_BUTTONMATRIX_CTRL_WIDTH_4, LV_BUTTONMATRIX_CTRL_WIDTH_4,
        LV_BUTTONMATRIX_CTRL_WIDTH_4, LV_BUTTONMATRIX_CTRL_WIDTH_4, LV_BUTTONMATRIX_CTRL_WIDTH_4,
        LV_BUTTONMATRIX_CTRL_WIDTH_4, LV_BUTTONMATRIX_CTRL_WIDTH_4, LV_BUTTONMATRIX_CTRL_WIDTH_4,
        LV_BUTTONMATRIX_CTRL_WIDTH_4, LV_BUTTONMATRIX_CTRL_WIDTH_4, LV_BUTTONMATRIX_CTRL_WIDTH_4,
        LV_BUTTONMATRIX_CTRL_WIDTH_6, LV_BUTTONMATRIX_CTRL_WIDTH_6,
    };
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_1, ipMap, ipCtrl);
}

}  // namespace

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
    initIpKeyboardMap(kb_);
    lv_obj_add_event_cb(kb_, keyboardEvent, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb_, keyboardEvent, LV_EVENT_CANCEL, NULL);
}

void PaxxKeyboard::showFor(lv_obj_t *textarea) {
    if (!kb_ || !textarea) return;

    const bool alreadyVisible = isVisible() && activeTa_ == textarea;
    activeTa_ = textarea;

    if (!alreadyVisible) {
        applyKeyboardMode(kb_, modeForTextarea(textarea));
        lv_keyboard_set_textarea(kb_, textarea);
        lv_obj_remove_flag(kb_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(kb_);
        notifyVisibility(true);
    }
}

void PaxxKeyboard::hide() {
    if (!kb_ || lv_obj_has_flag(kb_, LV_OBJ_FLAG_HIDDEN)) return;

    lv_obj_t *ta = activeTa_;
    lv_keyboard_set_textarea(kb_, NULL);
    lv_obj_add_flag(kb_, LV_OBJ_FLAG_HIDDEN);
    activeTa_ = nullptr;
    if (ta) lv_obj_clear_state(ta, LV_STATE_FOCUSED);
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

    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
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

void PaxxKeyboard::attach(lv_obj_t *textarea, PaxxKbMode mode) {
    if (!textarea) return;

    lv_obj_set_user_data(textarea, reinterpret_cast<void *>(static_cast<intptr_t>(mode)));
    lv_textarea_set_one_line(textarea, true);
    lv_textarea_set_max_length(textarea, 64);
    if (mode == PaxxKbMode::Password) lv_textarea_set_password_mode(textarea, true);
    lv_obj_set_style_anim_duration(textarea, 0, LV_PART_CURSOR);
    lv_obj_remove_flag(textarea, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_event_cb(textarea, textareaEvent, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(textarea, textareaEvent, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(textarea, textareaEvent, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(textarea, textareaEvent, LV_EVENT_CANCEL, NULL);
}
