#include "storage/Preferences.h"

PrinterProfile &activeProfile(AppConfig &cfg) {
    if (cfg.activeProfile < 0 || cfg.activeProfile >= cfg.profileCount) {
        cfg.activeProfile = 0;
    }
    return cfg.profiles[cfg.activeProfile];
}

const PrinterProfile &activeProfile(const AppConfig &cfg) {
    return const_cast<AppConfig &>(cfg).profiles[
        (cfg.activeProfile >= 0 && cfg.activeProfile < cfg.profileCount) ? cfg.activeProfile : 0];
}

PaxxPreferences &PaxxPreferences::instance() {
    static PaxxPreferences inst;
    return inst;
}

void PaxxPreferences::begin() {
    if (!loaded_) {
        prefs_.begin("paxxtouch", false);
        loaded_ = true;
    }
}

void PaxxPreferences::load(AppConfig &out) {
    begin();
    memset(&out, 0, sizeof(out));

    strlcpy(out.wifi.ssid, prefs_.getString("wifi_ssid", "").c_str(), sizeof(out.wifi.ssid));
    strlcpy(out.wifi.password, prefs_.getString("wifi_pass", "").c_str(), sizeof(out.wifi.password));

    out.profileCount = prefs_.getInt("prof_count", 0);
    out.activeProfile = prefs_.getInt("prof_active", 0);
    out.remoteScreenEnabled = prefs_.getBool("remote_scr", true);
    out.darkTheme = prefs_.getBool("dark_theme", true);
    out.notificationsEnabled = prefs_.getBool("notify", true);
    out.lastSpeedFactor = prefs_.getFloat("speed_fac", 100.0f);
    out.lastFlowFactor = prefs_.getFloat("flow_fac", 100.0f);

    if (out.profileCount <= 0) {
        out.profileCount = 1;
        strlcpy(out.profiles[0].name, "U1", sizeof(out.profiles[0].name));
        strlcpy(out.profiles[0].host, prefs_.getString("host", "").c_str(), sizeof(out.profiles[0].host));
        out.profiles[0].moonrakerPort = prefs_.getUShort("port", 7125);
        strlcpy(out.profiles[0].apiKey, prefs_.getString("api_key", "").c_str(), sizeof(out.profiles[0].apiKey));
        out.profiles[0].useAuth = prefs_.getBool("use_auth", false);
        strlcpy(out.profiles[0].username, prefs_.getString("user", "").c_str(), sizeof(out.profiles[0].username));
        strlcpy(out.profiles[0].password, prefs_.getString("pass", "").c_str(), sizeof(out.profiles[0].password));
        return;
    }

    out.profileCount = min(out.profileCount, PAXX_MAX_PROFILES);
    for (int i = 0; i < out.profileCount; ++i) {
        char key[16];
        PrinterProfile &p = out.profiles[i];
        snprintf(key, sizeof(key), "p%d_name", i);
        strlcpy(p.name, prefs_.getString(key, "Printer").c_str(), sizeof(p.name));
        snprintf(key, sizeof(key), "p%d_host", i);
        strlcpy(p.host, prefs_.getString(key, "").c_str(), sizeof(p.host));
        snprintf(key, sizeof(key), "p%d_port", i);
        p.moonrakerPort = prefs_.getUShort(key, 7125);
        snprintf(key, sizeof(key), "p%d_key", i);
        strlcpy(p.apiKey, prefs_.getString(key, "").c_str(), sizeof(p.apiKey));
        snprintf(key, sizeof(key), "p%d_auth", i);
        p.useAuth = prefs_.getBool(key, false);
        snprintf(key, sizeof(key), "p%d_user", i);
        strlcpy(p.username, prefs_.getString(key, "").c_str(), sizeof(p.username));
        snprintf(key, sizeof(key), "p%d_pass", i);
        strlcpy(p.password, prefs_.getString(key, "").c_str(), sizeof(p.password));
    }
}

void PaxxPreferences::save(const AppConfig &cfg) {
    begin();
    prefs_.putString("wifi_ssid", cfg.wifi.ssid);
    prefs_.putString("wifi_pass", cfg.wifi.password);
    prefs_.putInt("prof_count", cfg.profileCount);
    prefs_.putInt("prof_active", cfg.activeProfile);
    prefs_.putBool("remote_scr", cfg.remoteScreenEnabled);
    prefs_.putBool("dark_theme", cfg.darkTheme);
    prefs_.putBool("notify", cfg.notificationsEnabled);
    prefs_.putFloat("speed_fac", cfg.lastSpeedFactor);
    prefs_.putFloat("flow_fac", cfg.lastFlowFactor);

    const int count = min(cfg.profileCount, PAXX_MAX_PROFILES);
    for (int i = 0; i < count; ++i) {
        char key[16];
        const PrinterProfile &p = cfg.profiles[i];
        snprintf(key, sizeof(key), "p%d_name", i);
        prefs_.putString(key, p.name);
        snprintf(key, sizeof(key), "p%d_host", i);
        prefs_.putString(key, p.host);
        snprintf(key, sizeof(key), "p%d_port", i);
        prefs_.putUShort(key, p.moonrakerPort);
        snprintf(key, sizeof(key), "p%d_key", i);
        prefs_.putString(key, p.apiKey);
        snprintf(key, sizeof(key), "p%d_auth", i);
        prefs_.putBool(key, p.useAuth);
        snprintf(key, sizeof(key), "p%d_user", i);
        prefs_.putString(key, p.username);
        snprintf(key, sizeof(key), "p%d_pass", i);
        prefs_.putString(key, p.password);
    }

    if (count > 0) {
        prefs_.putString("host", cfg.profiles[cfg.activeProfile].host);
        prefs_.putUShort("port", cfg.profiles[cfg.activeProfile].moonrakerPort);
    }
}

bool PaxxPreferences::hasWifi() const {
    AppConfig cfg{};
    const_cast<PaxxPreferences *>(this)->load(cfg);
    return cfg.wifi.ssid[0] != '\0';
}

bool PaxxPreferences::hasPrinter() const {
    AppConfig cfg{};
    const_cast<PaxxPreferences *>(this)->load(cfg);
    return cfg.profileCount > 0 && activeProfile(cfg).host[0] != '\0';
}
