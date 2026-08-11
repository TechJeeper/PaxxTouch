#pragma once

#include <lvgl.h>

enum class PaxxKbMode : uint8_t { Text, Password, Number };

class PaxxKeyboard {
public:
    using VisibilityFn = void (*)(bool visible, void *userData);

    static void init(lv_obj_t *parent);
    static void attach(lv_obj_t *textarea, PaxxKbMode mode = PaxxKbMode::Text);
    static void hide();
    static bool isVisible();
    static void promptFor(lv_obj_t *textarea);
    static void setVisibilityListener(VisibilityFn fn, void *userData);

private:
    static void textareaEvent(lv_event_t *e);
    static void keyboardEvent(lv_event_t *e);
    static void showFor(lv_obj_t *textarea);
    static void notifyVisibility(bool visible);

    static lv_obj_t *kb_;
    static lv_obj_t *activeTa_;
    static VisibilityFn visibilityFn_;
    static void *visibilityUserData_;
};
