#pragma once

#include <lvgl.h>
#include <stdint.h>
#include <stddef.h>

// Decode PNG/JPEG for LVGL image widgets. Caller owns buffer (free with freeBuffer).
class ImageDecoder {
public:
    static void initWorker();
    static bool decodePng(const uint8_t *data, size_t len, uint8_t *&rgb888, int &w, int &h);
    static bool decodePngRgb565(const uint8_t *data, size_t len, uint16_t *&rgb565, int &w, int &h);
    /** Decode into caller-owned RGB565 buffer (w*h*2 bytes). No allocation. */
    static bool decodePngRgb565Into(const uint8_t *data, size_t len, uint16_t *dst, int &w, int &h);
    /** Same as decodePngRgb565Into but runs on the caller task (no imgDec worker hop). */
    static bool decodePngRgb565IntoSync(const uint8_t *data, size_t len, uint16_t *dst, int &w, int &h);
    static bool decodeJpeg(const uint8_t *data, size_t len, uint16_t *&rgb565, int &w, int &h);
    static bool decodeJpegInto(const uint8_t *data, size_t len, uint16_t *dst, int &w, int &h);
    static bool decodeJpegIntoSync(const uint8_t *data, size_t len, uint16_t *dst, int &w, int &h);
    static void freeBuffer(void *buf);
    static void bindLvImage(lv_image_dsc_t &dsc, lv_obj_t *image, const void *pixels,
                            lv_color_format_t format, int w, int h, int maxW = 480, int maxH = 320);
};
