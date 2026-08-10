# PaxxTouch Feature Status (v1.0.0)

## Completed

- [x] WiFi provisioning (scan, connect, NVS save)
- [x] Moonraker WebSocket with full U1 object subscription
- [x] Moonraker auth (API key + username/password login)
- [x] Multi-printer profiles (up to 5, cycle in Settings)
- [x] Print dashboard with all 4 extruder temperatures
- [x] Print controls (pause/resume/cancel)
- [x] Speed/flow adjustment (M220/M221 via sliders)
- [x] Filament assignment from `print_task_config`
- [x] Filament RFID write via Paxx `/printer/filament_detect/set`
- [x] Remote Screen PNG decode + live mirror + touch injection
- [x] Camera live snapshot from `/webcam/snapshot.jpg`
- [x] Timelapse browser via Moonraker file API
- [x] G-code file browser + start print
- [x] Controls: G28 home, BED_MESH_CALIBRATE
- [x] Firmware-config URL shortcut
- [x] Dark/light theme toggle
- [x] Push notifications (complete, error, paused)
- [x] Arduino OTA ready

## Future Enhancements

- [ ] In-app MP4 timelapse playback (ESP32 hardware decode)
- [ ] WebRTC camera stream (lower latency than snapshot polling)
- [ ] MQTT timelapse API direct integration
- [ ] Full multi-printer management UI (add/edit/delete profiles)
- [ ] Captive portal WiFi provisioning (basic scan/connect UI implemented)
- [ ] Prometheus metrics dashboard
- [ ] ESP-IDF port for tear-free display
