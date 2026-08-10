#include "paxx/ImageDecoder.h"

#include <PNGdec.h>
#include <TJpg_Decoder.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace {

PNG *gPng = nullptr;
uint16_t *gDecodeOut = nullptr;
uint8_t *gDecodeOut888 = nullptr;
SemaphoreHandle_t gDecodeMutex = nullptr;
bool gJpgInited = false;
int gDecodeW = 0;
int gDecodeH = 0;

void ensureJpegDecoder() {
    if (gJpgInited) return;
    TJpgDec.setJpgScale(1);
    TJpgDec.setSwapBytes(true);
    gJpgInited = true;
}

void decodeYield(int y) {
    if ((y & 0x1F) != 0) return;
    yield();
}

int pngDrawRgb888(PNGDRAW *pDraw) {
    if (!gDecodeOut888 || !gPng) return 0;
    if (pDraw->y >= gDecodeH) return 0;

    uint8_t *dst = gDecodeOut888 + static_cast<size_t>(pDraw->y) * static_cast<size_t>(gDecodeW) * 3;
    const uint8_t *s = pDraw->pPixels;

    switch (pDraw->iPixelType) {
        case PNG_PIXEL_TRUECOLOR:
            for (int x = 0; x < pDraw->iWidth; x++) {
                dst[x * 3 + 0] = s[0];
                dst[x * 3 + 1] = s[1];
                dst[x * 3 + 2] = s[2];
                s += 3;
            }
            break;
        case PNG_PIXEL_TRUECOLOR_ALPHA:
            for (int x = 0; x < pDraw->iWidth; x++) {
                dst[x * 3 + 0] = s[0];
                dst[x * 3 + 1] = s[1];
                dst[x * 3 + 2] = s[2];
                s += 4;
            }
            break;
        default: {
            uint16_t line[640];
            gPng->getLineAsRGB565(pDraw, line, PNG_RGB565_LITTLE_ENDIAN, 0xFFFFFFFF);
            for (int x = 0; x < pDraw->iWidth; x++) {
                const uint16_t px = line[x];
                dst[x * 3 + 0] = static_cast<uint8_t>((px >> 11) << 3);
                dst[x * 3 + 1] = static_cast<uint8_t>(((px >> 5) & 0x3F) << 2);
                dst[x * 3 + 2] = static_cast<uint8_t>((px & 0x1F) << 3);
            }
            break;
        }
    }

    decodeYield(pDraw->y);
    return 1;
}

bool jpgOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
    if (!gDecodeOut || !bitmap) return 0;
    for (uint16_t row = 0; row < h; ++row) {
        if (y + row >= gDecodeH) break;
        memcpy(gDecodeOut + (y + row) * gDecodeW + x, bitmap + row * w, w * sizeof(uint16_t));
        decodeYield(y + row);
    }
    return 1;
}

bool decodeSizeOk(int w, int h) {
    if (w <= 0 || h <= 0 || w > 640 || h > 480) return false;
    return static_cast<size_t>(w) * static_cast<size_t>(h) * 2 <= 640 * 480 * 2;
}

bool decodeSizeOkRgb888(int w, int h) {
    if (w <= 0 || h <= 0 || w > 640 || h > 480) return false;
    return static_cast<size_t>(w) * static_cast<size_t>(h) * 3 <= 640UL * 480UL * 3;
}

bool decodePngInternal(const uint8_t *data, size_t len, uint8_t *&rgb888, int &w, int &h) {
    rgb888 = nullptr;
    if (!data || len < 8) return false;

    PNG png;
    gPng = &png;
    const int rcOpen = png.openRAM(const_cast<uint8_t *>(data), len, pngDrawRgb888);
    if (rcOpen != PNG_SUCCESS) {
        gPng = nullptr;
        Serial.printf("[Image] PNG open failed rc=%d len=%u\n", rcOpen, static_cast<unsigned>(len));
        return false;
    }

    w = png.getWidth();
    h = png.getHeight();
    if (!decodeSizeOkRgb888(w, h)) {
        Serial.printf("[Image] PNG size rejected %dx%d\n", w, h);
        png.close();
        gPng = nullptr;
        return false;
    }

    gDecodeW = w;
    gDecodeH = h;

    const size_t bytes = static_cast<size_t>(w) * h * 3;
    gDecodeOut888 = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!gDecodeOut888) gDecodeOut888 = static_cast<uint8_t *>(malloc(bytes));
    if (!gDecodeOut888) {
        Serial.printf("[Image] PNG alloc failed %u bytes\n", static_cast<unsigned>(bytes));
        png.close();
        gPng = nullptr;
        return false;
    }

    const int rcDecode = png.decode(nullptr, 0);
    png.close();
    gPng = nullptr;
    if (rcDecode != PNG_SUCCESS) {
        Serial.printf("[Image] PNG decode failed rc=%d\n", rcDecode);
        free(gDecodeOut888);
        gDecodeOut888 = nullptr;
        return false;
    }

    rgb888 = gDecodeOut888;
    gDecodeOut888 = nullptr;
    Serial.printf("[Image] PNG ok %dx%d RGB888 (%u bytes)\n", w, h, static_cast<unsigned>(bytes));
    return true;
}

bool decodeJpegInternal(const uint8_t *data, size_t len, uint16_t *&rgb565, int &w, int &h) {
    rgb565 = nullptr;
    if (!data || len < 4) return false;

    ensureJpegDecoder();

    uint16_t uw = 0, uh = 0;
    if (TJpgDec.getJpgSize(&uw, &uh, const_cast<uint8_t *>(data), len) != JDR_OK) {
        Serial.printf("[Image] JPEG size read failed (len=%u %02x %02x)\n",
                      static_cast<unsigned>(len), data[0], data[1]);
        return false;
    }

    w = uw;
    h = uh;
    if (!decodeSizeOk(w, h)) {
        Serial.printf("[Image] JPEG size rejected %dx%d\n", w, h);
        return false;
    }

    gDecodeW = w;
    gDecodeH = h;

    const size_t bytes = static_cast<size_t>(w) * h * sizeof(uint16_t);
    gDecodeOut = static_cast<uint16_t *>(heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!gDecodeOut) gDecodeOut = static_cast<uint16_t *>(calloc(1, bytes));
    if (!gDecodeOut) {
        Serial.printf("[Image] JPEG alloc failed %u bytes\n", static_cast<unsigned>(bytes));
        return false;
    }

    TJpgDec.setCallback(jpgOutput);
    TJpgDec.setJpgScale(1);
    if (TJpgDec.drawJpg(0, 0, const_cast<uint8_t *>(data), len) != JDR_OK) {
        Serial.println("[Image] JPEG decode failed");
        free(gDecodeOut);
        gDecodeOut = nullptr;
        return false;
    }

    rgb565 = gDecodeOut;
    gDecodeOut = nullptr;
    Serial.printf("[Image] JPEG ok %dx%d\n", w, h);
    return true;
}

enum class DecodeKind { Png, Jpeg };

struct DecodeJob {
    const uint8_t *data;
    size_t len;
    DecodeKind kind;
    void *out;
    int w;
    int h;
    bool ok;
    SemaphoreHandle_t done;
};

void decodeWorker(void *arg) {
    auto *job = static_cast<DecodeJob *>(arg);
    job->out = nullptr;
    job->w = job->h = 0;

    if (job->kind == DecodeKind::Png) {
        uint8_t *buf = nullptr;
        job->ok = decodePngInternal(job->data, job->len, buf, job->w, job->h);
        job->out = buf;
    } else {
        uint16_t *buf = nullptr;
        job->ok = decodeJpegInternal(job->data, job->len, buf, job->w, job->h);
        job->out = buf;
    }

    xSemaphoreGive(job->done);
    vTaskDelete(nullptr);
}

bool runDecodeOnWorker(DecodeKind kind, const uint8_t *data, size_t len, void *&out, int &w, int &h) {
    out = nullptr;
    w = h = 0;
    if (!data || len == 0) return false;

    if (!gDecodeMutex) gDecodeMutex = xSemaphoreCreateMutex();
    if (!gDecodeMutex) return false;
    if (xSemaphoreTake(gDecodeMutex, pdMS_TO_TICKS(35000)) != pdTRUE) {
        Serial.println("[Image] decode mutex timeout");
        return false;
    }

    DecodeJob job{};
    job.data = data;
    job.len = len;
    job.kind = kind;
    job.done = xSemaphoreCreateBinary();
    if (!job.done) {
        xSemaphoreGive(gDecodeMutex);
        return false;
    }

    constexpr uint32_t kStackWords = 65536 / sizeof(StackType_t);
    if (xTaskCreate(decodeWorker, "imgDec", kStackWords, &job, 1, nullptr) != pdPASS) {
        vSemaphoreDelete(job.done);
        Serial.println("[Image] decode task create failed");
        xSemaphoreGive(gDecodeMutex);
        return false;
    }

    const bool finished = xSemaphoreTake(job.done, pdMS_TO_TICKS(30000)) == pdTRUE;
    vSemaphoreDelete(job.done);

    if (!finished) {
        Serial.println("[Image] decode task timeout");
        xSemaphoreGive(gDecodeMutex);
        return false;
    }

    if (job.ok) {
        out = job.out;
        w = job.w;
        h = job.h;
    }
    xSemaphoreGive(gDecodeMutex);
    return job.ok;
}

}  // namespace

bool ImageDecoder::decodePng(const uint8_t *data, size_t len, uint8_t *&rgb888, int &w, int &h) {
    void *out = nullptr;
    const bool ok = runDecodeOnWorker(DecodeKind::Png, data, len, out, w, h);
    rgb888 = static_cast<uint8_t *>(out);
    return ok;
}

bool ImageDecoder::decodeJpeg(const uint8_t *data, size_t len, uint16_t *&rgb565, int &w, int &h) {
    void *out = nullptr;
    const bool ok = runDecodeOnWorker(DecodeKind::Jpeg, data, len, out, w, h);
    rgb565 = static_cast<uint16_t *>(out);
    return ok;
}

void ImageDecoder::freeBuffer(void *buf) {
    if (buf) free(buf);
}

void ImageDecoder::bindLvImage(lv_image_dsc_t &dsc, lv_obj_t *image, const void *pixels,
                               lv_color_format_t format, int w, int h, int maxW, int maxH) {
    if (!image || !pixels || w <= 0 || h <= 0) return;

    lv_image_set_src(image, NULL);

    const uint32_t pxSize = LV_COLOR_FORMAT_GET_SIZE(format);
    if (pxSize == 0) return;

    dsc = {};
    dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc.header.cf = format;
    dsc.header.flags = 0;
    dsc.header.w = static_cast<uint32_t>(w);
    dsc.header.h = static_cast<uint32_t>(h);
    dsc.header.stride = static_cast<uint32_t>(w * pxSize);
    dsc.data = static_cast<const uint8_t *>(pixels);
    dsc.data_size = static_cast<uint32_t>(w * h * pxSize);

    lv_image_set_src(image, &dsc);
    lv_obj_center(image);

    if (maxW > 0 && maxH > 0) {
        const int scaleX = (maxW * 256) / w;
        const int scaleY = (maxH * 256) / h;
        const int scale = scaleX < scaleY ? scaleX : scaleY;
        lv_image_set_scale(image, static_cast<uint32_t>(scale));
    } else {
        lv_image_set_scale(image, 256);
    }
}
