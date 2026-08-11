#pragma once

/**
 * PaxxTouch build variants.
 *
 * PAXX_REMOTE_ONLY — WiFi + printer config + fullscreen Remote Screen only.
 * Skips Moonraker WebSocket, camera, thumbnails, and the full menu UI.
 */

#ifndef PAXX_REMOTE_ONLY
#define PAXX_REMOTE_ONLY 0
#endif

#if PAXX_REMOTE_ONLY
#define PAXX_VARIANT_NAME "remote"
#else
#define PAXX_VARIANT_NAME "full"
#endif

inline void paxxFormatScreenUrl(const char *host, char *out, size_t outLen) {
    if (!out || outLen == 0) return;
    if (!host || !host[0]) {
        snprintf(out, outLen, "http://<printer-ip>/screen/");
        return;
    }
    snprintf(out, outLen, "http://%s/screen/", host);
}
