#pragma once

#include <lvgl.h>
#include <stdint.h>
#include <stddef.h>

// Decode PNG/JPEG for LVGL image widgets. Caller owns buffer (free with freeBuffer).
class ImageDecoder {
public:
    static bool decodePng(const uint8_t *data, size_t len, uint8_t *&rgb888, int &w, int &h);
    static bool decodeJpeg(const uint8_t *data, size_t len, uint16_t *&rgb565, int &w, int &h);
    static void freeBuffer(void *buf);
    static void bindLvImage(lv_image_dsc_t &dsc, lv_obj_t *image, const void *pixels,
                            lv_color_format_t format, int w, int h, int maxW = 480, int maxH = 320);
};
