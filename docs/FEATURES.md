# PaxxTouch Feature Status

## v0.2.5 — Restored Working Display Pipeline & /screen/ Endpoint

- Restore working v0.1.9 display flush pipeline and single full framebuffer mode (`PT_LVGL_RENDER_FULL_1`)
- Set primary U1 remote screen snapshot endpoint to `/screen/`

## v0.2.4 — Double-Buffered Rendering & Solid Hardware Init

- Enable double-buffered full framebuffers in PSRAM (`PT_LVGL_RENDER_FULL_2`), eliminating atomic redraw collisions, color flipping, and partial invalidation tearing
- Restore factory hardware ST7701S panel clock & sync parameters (`pclk_active_neg = 1`, 14.8 MHz PCLK), eliminating white screen on boot

## v0.2.2 — Display Jitter & Color Phase Fix

- Align PCLK active edge sampling to rising edge (`pclk_active_neg = 0`), eliminating RGB setup/hold timing violations that caused blue buttons to turn green and screen jitter while idle
- Calibrate PCLK frequency to 12.0 MHz and optimize sync pulse widths

## v0.2.1 — ST7701S Hardware Shift Fix & Boot Screen

- ST7701S RGB panel HSYNC / VSYNC timing calibration (eliminates horizontal left/right display shifting under PSRAM & WiFi load)
- Increased SRAM bounce buffer cushion (20 lines) to prevent LCD DMA FIFO underflow
- Instant 800x480 boot splash screen generated from `@boot.bmp`

## v0.2.0 — Config UI Glitch Fix & Performance Update

- Chunked block blitting for display flushes (eliminates mid-frame tearing and UI glitching during typing, text cursor blinking, and WiFi status updates)
- Dynamic render mode stride and memory offset calculation for LVGL 9 display flush callback

## v0.1.9 — Remote Screen responsiveness and lag fix

- Triple Buffering pipeline (eliminates frame corruption and decode failures during fast polling)
- Non-blocking fire-and-forget touch POSTs (reduces touch dispatch latency from 80ms to 1-2ms)
- Post-touch burst polling at 20ms intervals for 1.5s following touch actions
- Correct ETag / `If-None-Match` header formatting for Nginx and Python `fb-http-server`
- Immediate UI blitting of decoded frames on main loop tick

## v0.1.8 — Display flush color fix

- Fix display flush to swap R/B into a line buffer without mutating LVGL draw memory (fixes intermittent wrong colors on full-frame mode)

## v0.1.7 — LVGL 9 timer API and UI refresh

- Use `lv_timer_handler()` (LVGL 9) instead of deprecated `lv_task_handler()`
- Loading spinner and form layout polish

## v0.1.6 — Color and UI fixes

- Align JPEG decode byte order with PNG (fixes intermittent red/blue swap on remote mirror)
- UI keyboard and screen refinements

## v0.1.5 — Theme and screen tweaks

- Theme styling updates and remote/setup screen refinements

## v0.1.4 — UI polish

- Theme, keyboard, and setup/remote screen layout improvements

## v0.1.3 — Web flasher branding

- PaxxTouch logo on web flasher page

## v0.1.2 — Display color fix

- Fix red/blue channel swap on K-Touch / PandaTouch RGB565 panel (UI + remote mirror)

## v0.1.1 — Web flasher fix

- Web flasher preserves PlatformIO bootloader flash headers (fixes blank screen after successful web flash)
- Bundled firmware synced with `pio` build

## v0.1.0 — PaxxTouch Remote (shipped)

Default build: `paxxtouch-remote`

- [x] WiFi provisioning (scan, connect, NVS save)
- [x] Printer connection (IP, Moonraker port, auth, API key)
- [x] Moonraker REST login for nginx-authenticated Remote Screen
- [x] Fullscreen U1 panel mirror (480×320 PNG/JPEG snapshots)
- [x] Touch forwarding (`POST /screen/touch`)
- [x] HTTP keep-alive polling + ETag 304 skip
- [x] Pipelined decode (poll never blocks on PNG/JPEG)
- [x] Instant snapshot after touch
- [x] JPEG endpoint probe when available
- [x] Gear menu: WiFi, Printer, About, return to mirror
- [x] Dark/light theme
- [x] Web flasher + GitHub Releases

## Legacy full UI (`paxxtouch` build)

Still compilable, not the default release:

- [x] Moonraker WebSocket + print dashboard
- [x] Filament / Files / Camera / Timelapse / Controls screens
- [x] Push notifications, multi-printer profiles, OTA

## Future enhancements

- [ ] WebSocket transport (`fb-http-ws` on port 8093) if LAN-accessible
- [ ] Direct framebuffer blit (bypass LVGL invalidate for mirror)
- [ ] Captive portal WiFi provisioning
- [ ] ESP-IDF port for tear-free display ([PandaTouch_IDF](https://github.com/bigtreetech/PandaTouch_IDF))
