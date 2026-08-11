# PaxxTouch Feature Status

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
