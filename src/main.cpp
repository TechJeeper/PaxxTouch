#include "pt/pt_display.h"
#include "paxx/BuildConfig.h"
#include "ui/App.h"
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>

static PaxxApp app;

void setup() {
    Serial.begin(115200);
    delay(500);
#if PAXX_REMOTE_ONLY
    Serial.println("PaxxTouch Remote boot");
#else
    Serial.println("PaxxTouch boot");
#endif
    // Keep WiFi/modem out of power-save — PS can stall RGB panel DMA under load.
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(WIFI_PS_NONE);
    esp_wifi_set_ps(WIFI_PS_NONE);

    // PSRAM LVGL buffers leave internal DRAM for bounce + WiFi.
    pt_setup_display(PT_LVGL_RENDER_PARTIAL_2_PSRAM);
    Serial.printf("Display ready (internal free=%u psram free=%u)\n",
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    app.begin();
}

void loop() {
    app.loop();
}
