#pragma once
#ifndef PT_DISPLAY_H
#define PT_DISPLAY_H

/* =========================
 *  Includes
 * ========================= */
#include <Arduino.h>
#include <algorithm>
#include <driver/ledc.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include <TJpg_Decoder.h>
#include "TAMC_GT911.h"
#include "pt_board.h"
#if __has_include("boot_logo.h")
#include "boot_logo.h"
#define PT_HAS_BOOT_LOGO 1
#else
#define PT_HAS_BOOT_LOGO 0
#endif

/** K-Touch / PandaTouch RGB565 panel expects BGR channel order in each pixel word. */
inline uint16_t pt_rgb565_swap_rb(uint16_t c) {
  return static_cast<uint16_t>((c & 0x07E0) | ((c & 0x001F) << 11) | ((c & 0xF800) >> 11));
}

extern Arduino_RGB_Display pt_gfx;

#if PT_HAS_BOOT_LOGO
// TJpg delivers MCU tiles (typically <=16x16). Keep this tiny — never a full row buffer.
static bool pt_boot_jpg_draw(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  if (w == 0 || h == 0 || w > 16 || h > 16) return false;
  uint16_t block_buf[16 * 16];
  const uint32_t count = static_cast<uint32_t>(w) * h;
  for (uint32_t i = 0; i < count; ++i) {
    block_buf[i] = pt_rgb565_swap_rb(bitmap[i]);
  }
  pt_gfx.draw16bitRGBBitmap(x, y, block_buf, w, h);
  return true;
}
#endif

#ifndef PT_LVGL_RENDER_PARTIAL_LINES
#define PT_LVGL_RENDER_PARTIAL_LINES 80
#endif

#ifndef PT_LCD_RENDER_BOUNCE_LINES
// SRAM bounce cushions PSRAM DMA under WiFi (stops horizontal shift).
// Keep LVGL draw buffers out of internal RAM so the ISR can keep up.
#define PT_LCD_RENDER_BOUNCE_LINES 20
#endif

/* =========================
 *  Types / Render modes
 * ========================= */

// Function pointer type for scheduling work on the LVGL thread
typedef void (*pt_ui_fn_t)(void *arg);

// Render method enum (mirrors board Kconfig options)
typedef enum
{
  PT_LVGL_RENDER_FULL_1 = 0,
  PT_LVGL_RENDER_FULL_2,
  PT_LVGL_RENDER_PARTIAL_1,
  PT_LVGL_RENDER_PARTIAL_2,
  PT_LVGL_RENDER_PARTIAL_1_PSRAM,
  PT_LVGL_RENDER_PARTIAL_2_PSRAM,
} PT_LVGL_render_method_t;

// Default render method when not provided by build system
#ifndef PT_LVGL_RENDER_METHOD
#define PT_LVGL_RENDER_METHOD PT_LVGL_RENDER_PARTIAL_2
#endif

/* =========================
 *  Display / Touch Objects
 * ========================= */
extern TAMC_GT911 pt_touchpanel;

#if defined(ESP_ARDUINO_VERSION_MAJOR)
extern Arduino_ESP32RGBPanel pt_rgbpanel;
#endif

/* =========================
 *  State / LVGL Buffers
 * ========================= */

extern lv_color_t *pt_disp_draw_buf;
extern lv_color_t *pt_disp_draw_buf2;

/* =========================
 *  Backlight (LEDC) Config
 * ========================= */

static uint8_t pt_backlight_percent = 100;

/* =========================
 *  Helpers
 * ========================= */

/**
 * @brief Returns the number of milliseconds since the program started.
 *
 * This inline function calls the underlying `millis()` function to retrieve
 * the elapsed time in milliseconds. Useful for timing and scheduling tasks.
 *
 * @return uint32_t Number of milliseconds since the program started.
 */
inline uint32_t millis_cb()
{
  return millis();
}

/**
 * @brief Converts a brightness percentage to LEDC duty cycle value.
 *
 * This function takes a percentage value (0-100) and calculates the corresponding
 * duty cycle for the LEDC (LED Controller) based on a timer with 11-bit resolution.
 * The duty cycle determines the brightness of the backlight.
 *
 * @param percent Brightness percentage (0-100).
 * @return Calculated duty cycle value for the LEDC. Returns 0 if the computed duty is less than 1.
 */
static uint32_t pt_get_duty_from_percent(uint32_t percent)
{
  uint32_t duty = (uint32_t)(((float)percent / 100.0f) * ((1 << LEDC_TIMER_11_BIT) - 1));
  return (duty < 1) ? 0 : duty;
}

/**
 * @brief Sets the backlight brightness to a specified percentage.
 *
 * This inline function adjusts the backlight brightness by setting the LEDC duty cycle
 * based on the provided percentage. It ensures the percentage is within the valid range (0-100).
 * If `save` is true, it updates the global `pt_backlight_percent` variable to remember
 * the current brightness setting.
 *
 * @param percent Brightness percentage (0-100).
 * @param save If true, saves the current brightness setting.
 */
inline void pt_set_backlight(uint8_t percent, bool save)
{
  if (percent > 100)
    percent = 100;
  uint32_t target_duty = pt_get_duty_from_percent(percent);
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, target_duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
  if (save)
  {
    pt_backlight_percent = percent;
  }
}

/**
 * @brief Initializes the backlight control.
 *
 * This function sets up the LEDC timer and channel for controlling the backlight brightness.
 * It also sets the initial brightness level.
 *
 * @param set_percent Initial brightness percentage (0-100).
 */
static void pt_init_backlight(uint8_t set_percent)
{
  // Initialize backlight hardware; bring up at 0% briefly to avoid LCD reset flash.
  ledc_timer_config_t timer_config = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = LEDC_TIMER_11_BIT,
      .timer_num = LEDC_TIMER_1,
      .freq_hz = PT_LCD_BL_FREQUENCY_HZ,
      .clk_cfg = LEDC_USE_APB_CLK};
  ledc_timer_config(&timer_config);

  ledc_channel_config_t channel_config = {
      .gpio_num = PT_LCD_BL_PIN,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = LEDC_CHANNEL_0,
      .intr_type = LEDC_INTR_DISABLE,
      .timer_sel = LEDC_TIMER_1,
      .duty = 0,
      .hpoint = 0};
  ledc_channel_config(&channel_config);

  ledc_fade_func_install(0);

  if (set_percent > 100) set_percent = 100;
  pt_set_backlight(0, false);
  if (set_percent > 0) {
    pt_set_backlight(set_percent, true);
  } else {
    pt_backlight_percent = 0;
  }
}

/** Keep the panel lit — RGB backlight has no wake path except power cycle once off. */
inline void pt_keep_display_awake()
{
  if (pt_backlight_percent < 1) pt_backlight_percent = 100;
  pt_set_backlight(pt_backlight_percent, false);
}

/* =========================
 *  LVGL Callbacks
 * ========================= */

/**
 * @brief Flushes the display buffer to the screen.
 *
 * This function is called by LVGL to update the display with the contents of the
 * provided pixel map. It uses the graphics library to draw the bitmap on the screen.
 *
 * @param disp Pointer to the LVGL display object.
 * @param area Pointer to the area to be updated.
 * @param px_map Pointer to the pixel map data.
 */
static bool pt_is_full_render = false;

inline void pt_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
  const int32_t x1 = area->x1;
  const int32_t y1 = area->y1;
  const uint32_t w = lv_area_get_width(area);
  const uint32_t h = lv_area_get_height(area);
  if (w == 0 || h == 0 || w > PT_LCD_H_RES || h > PT_LCD_V_RES) {
    lv_disp_flush_ready(disp);
    return;
  }

  const bool is_full_mode = pt_is_full_render;
  const uint32_t screen_w = lv_display_get_horizontal_resolution(disp);
  const uint32_t stride = is_full_mode ? screen_w : w;
  const uint16_t *src_base = reinterpret_cast<const uint16_t *>(px_map) +
                             (is_full_mode ? (static_cast<uint32_t>(y1) * screen_w + static_cast<uint32_t>(x1)) : 0);

  // Chunked R/B channel swapping block buffer (up to 16 lines at once).
  static uint16_t block_buf[PT_LCD_H_RES * 16];

  for (uint32_t row_offset = 0; row_offset < h; row_offset += 16) {
    const uint32_t chunk_h = (h - row_offset < 16) ? (h - row_offset) : 16;
    for (uint32_t r = 0; r < chunk_h; ++r) {
      const uint16_t *src_row = src_base + (row_offset + r) * stride;
      uint16_t *dst_row = block_buf + r * w;
      for (uint32_t col = 0; col < w; ++col) {
        dst_row[col] = pt_rgb565_swap_rb(src_row[col]);
      }
    }
    pt_gfx.draw16bitRGBBitmap(x1, y1 + static_cast<int32_t>(row_offset), block_buf, w, chunk_h);
  }

  lv_disp_flush_ready(disp);
}

/**
 * @brief Reads touchpad input data.
 *
 * This function is called by LVGL to read touch input data from the touch panel.
 *
 * @param indev Pointer to the LVGL input device object.
 * @param data Pointer to the LVGL input device data structure.
 */
inline void pt_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
  pt_touchpanel.read(); // Read touch data
  if (pt_touchpanel.isTouched)
  {
    // Any touch restores backlight (no HW wake if PWM duty collapsed).
    pt_keep_display_awake();
    for (int i = 0; i < pt_touchpanel.touches; i++)
    {
      if (i == 0)
      {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = pt_touchpanel.points[i].x;
        data->point.y = pt_touchpanel.points[i].y;
      }
    }
  }
  else
  {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

/* =========================
 *  Public API
 * ========================= */

/**
 * @brief Sets up the display.
 *
 * This function initializes the display settings and prepares it for use.
 */
inline void pt_setup_display(PT_LVGL_render_method_t mode = (PT_LVGL_render_method_t)PT_LVGL_RENDER_METHOD)
{

  uint32_t screenWidth;
  uint32_t screenHeight;
  uint32_t bufSize;
  lv_display_t *disp;

  pinMode(PT_LCD_RESET_PIN, OUTPUT);
  digitalWrite(PT_LCD_RESET_PIN, 0);
  delay(100);
  digitalWrite(PT_LCD_RESET_PIN, 1);
  delay(10);

  // Backlight setup — stay fully on (no auto-sleep / dim path in this firmware).
  pt_init_backlight(100);
  pt_keep_display_awake();

  // Panel bring-up
  pt_gfx.begin();
  pt_gfx.fillScreen(0x0000);
#if PT_HAS_BOOT_LOGO
  // JPEG is PROGMEM-only; decode streams MCU tiles (no full-frame RAM).
  TJpgDec.setCallback(pt_boot_jpg_draw);
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(false);
  TJpgDec.drawJpg(0, 0, boot_logo_jpg, sizeof(boot_logo_jpg));
#endif

  // Touch
  pt_touchpanel.begin(GT911_ADDR1);
  Wire.setClock(PT_I2C0_SPEED);
  pt_touchpanel.setRotation(ROTATION_INVERTED);

  // LVGL core
  lv_init();
  lv_tick_set_cb(millis_cb);

  screenWidth = pt_gfx.width();
  screenHeight = pt_gfx.height();

  // Decide buffer strategy based on render method
  // A small helper for allocation with preferred PSRAM or internal
  auto alloc_buf = [&](size_t count, bool prefer_psram) -> lv_color_t *
  {
    int caps = MALLOC_CAP_8BIT;
    if (prefer_psram)
      caps |= MALLOC_CAP_SPIRAM;
    return (lv_color_t *)heap_caps_malloc(count * sizeof(lv_color_t), caps);
  };

  switch (mode)
  {
  case PT_LVGL_RENDER_FULL_1:
    // Single full framebuffer in PSRAM
    bufSize = screenWidth * screenHeight;
    pt_disp_draw_buf = alloc_buf(bufSize, true);
    if (pt_disp_draw_buf)
    {
      pt_is_full_render = true;
      disp = lv_display_create(screenWidth, screenHeight);
      lv_display_set_flush_cb(disp, pt_disp_flush);
      lv_display_set_buffers(disp, pt_disp_draw_buf, NULL, bufSize * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_FULL);
      break;
    }

    // If allocation failed, fall through to try other strategies
  case PT_LVGL_RENDER_FULL_2:
    // Double full framebuffers in PSRAM
    bufSize = screenWidth * screenHeight;
    pt_disp_draw_buf = alloc_buf(bufSize, true);
    if (pt_disp_draw_buf)
    {
      pt_disp_draw_buf2 = alloc_buf(bufSize, true);
    }
    if (pt_disp_draw_buf && pt_disp_draw_buf2)
    {
      pt_is_full_render = true;
      disp = lv_display_create(screenWidth, screenHeight);
      lv_display_set_flush_cb(disp, pt_disp_flush);
      lv_display_set_buffers(disp, pt_disp_draw_buf, pt_disp_draw_buf2, bufSize * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_FULL);
      break;
    }
    // If allocation failed, fallback to partial

  case PT_LVGL_RENDER_PARTIAL_1:
    // Small partial buffer in internal RAM preferred
    bufSize = screenWidth * PT_LVGL_RENDER_PARTIAL_LINES;
    pt_disp_draw_buf = alloc_buf(bufSize, false);
    if (!pt_disp_draw_buf)
    {
      // Try PSRAM if internal allocation fails
      pt_disp_draw_buf = alloc_buf(bufSize, true);
    }
    if (!pt_disp_draw_buf)
    {
      return; // nothing we can do
    }
    disp = lv_display_create(screenWidth, screenHeight);
    lv_display_set_flush_cb(disp, pt_disp_flush);
    lv_display_set_buffers(disp, pt_disp_draw_buf, NULL, bufSize * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_PARTIAL);
    break;

  case PT_LVGL_RENDER_PARTIAL_2:
    // Two partial buffers in internal RAM preferred
    bufSize = screenWidth * PT_LVGL_RENDER_PARTIAL_LINES;
    pt_disp_draw_buf = alloc_buf(bufSize, false);
    if (pt_disp_draw_buf)
      pt_disp_draw_buf2 = alloc_buf(bufSize, false);
    if (!pt_disp_draw_buf2)
    {
      // try PSRAM for second buffer
      if (pt_disp_draw_buf)
        pt_disp_draw_buf2 = alloc_buf(bufSize, true);
    }
    // If we still don't have a second buffer, fall back to single partial
    disp = lv_display_create(screenWidth, screenHeight);
    lv_display_set_flush_cb(disp, pt_disp_flush);
    if (pt_disp_draw_buf && pt_disp_draw_buf2)
    {
      lv_display_set_buffers(disp, pt_disp_draw_buf, pt_disp_draw_buf2, bufSize * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_PARTIAL);
    }
    else if (pt_disp_draw_buf)
    {
      lv_display_set_buffers(disp, pt_disp_draw_buf, NULL, bufSize * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_PARTIAL);
    }
    else
    {
      return; // allocation failed
    }
    break;

  case PT_LVGL_RENDER_PARTIAL_1_PSRAM:
    // Small partial buffer in PSRAM
    bufSize = screenWidth * PT_LVGL_RENDER_PARTIAL_LINES;
    pt_disp_draw_buf = alloc_buf(bufSize, true);
    if (!pt_disp_draw_buf)
    {
      // try internal
      pt_disp_draw_buf = alloc_buf(bufSize, false);
    }
    if (!pt_disp_draw_buf)
    {
      return;
    }
    disp = lv_display_create(screenWidth, screenHeight);
    lv_display_set_flush_cb(disp, pt_disp_flush);
    lv_display_set_buffers(disp, pt_disp_draw_buf, NULL, bufSize * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_PARTIAL);
    break;

  case PT_LVGL_RENDER_PARTIAL_2_PSRAM:
    // Two partial buffers in PSRAM preferred
    bufSize = screenWidth * PT_LVGL_RENDER_PARTIAL_LINES;
    pt_disp_draw_buf = alloc_buf(bufSize, true);
    if (pt_disp_draw_buf)
      pt_disp_draw_buf2 = alloc_buf(bufSize, true);
    if (!pt_disp_draw_buf2)
    {
      // try mixed: first internal then psram
      if (pt_disp_draw_buf)
        pt_disp_draw_buf2 = alloc_buf(bufSize, false);
    }
    disp = lv_display_create(screenWidth, screenHeight);
    lv_display_set_flush_cb(disp, pt_disp_flush);
    if (pt_disp_draw_buf && pt_disp_draw_buf2)
    {
      lv_display_set_buffers(disp, pt_disp_draw_buf, pt_disp_draw_buf2, bufSize * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_PARTIAL);
    }
    else if (pt_disp_draw_buf)
    {
      lv_display_set_buffers(disp, pt_disp_draw_buf, NULL, bufSize * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_PARTIAL);
    }
    else
    {
      return; // allocation failed
    }
    break;

  default:
    // Unknown mode: fallback to partial double-buffer default
    bufSize = screenWidth * PT_LVGL_RENDER_PARTIAL_LINES;
    pt_disp_draw_buf = alloc_buf(bufSize, false);
    if (pt_disp_draw_buf)
      pt_disp_draw_buf2 = alloc_buf(bufSize, false);
    disp = lv_display_create(screenWidth, screenHeight);
    lv_display_set_flush_cb(disp, pt_disp_flush);
    if (pt_disp_draw_buf && pt_disp_draw_buf2)
      lv_display_set_buffers(disp, pt_disp_draw_buf, pt_disp_draw_buf2, bufSize * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_PARTIAL);
    else if (pt_disp_draw_buf)
      lv_display_set_buffers(disp, pt_disp_draw_buf, NULL, bufSize * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_PARTIAL);
    else
      return;
  }

  // Touch input device
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, pt_touchpad_read);
  lv_indev_set_display(indev, disp);
}

/**
 * @brief Main loop for display tasks.
 *
 * This function is called repeatedly to handle
 * refreshing the screen and processing input events.
 */
inline void pt_loop_display()
{
  static uint32_t lastKeepAwakeMs = 0;
  lv_timer_handler();
  // Reassert backlight periodically so the panel cannot "go to sleep" silently.
  const uint32_t now = millis();
  if (now - lastKeepAwakeMs >= 3000) {
    lastKeepAwakeMs = now;
    pt_keep_display_awake();
  }
}

#endif // PT_DISPLAY_H
