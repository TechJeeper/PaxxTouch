#include "pt/pt_display.h"
#include "ui/App.h"

static PaxxApp app;

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("PaxxTouch boot");
    pt_setup_display(PT_LVGL_RENDER_FULL_1);
    Serial.println("Display ready");
    app.begin();
}

void loop() {
    app.loop();
}
