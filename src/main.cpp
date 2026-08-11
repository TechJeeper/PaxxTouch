#include "pt/pt_display.h"
#include "paxx/BuildConfig.h"
#include "ui/App.h"

static PaxxApp app;

void setup() {
    Serial.begin(115200);
    delay(500);
#if PAXX_REMOTE_ONLY
    Serial.println("PaxxTouch Remote boot");
#else
    Serial.println("PaxxTouch boot");
#endif
    pt_setup_display(PT_LVGL_RENDER_FULL_1);
    Serial.println("Display ready");
    app.begin();
}

void loop() {
    app.loop();
}
