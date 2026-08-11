#include "ui/App.h"
#include "ui/Theme.h"
#include "ui/Notify.h"
#include "ui/Keyboard.h"
#include "ui/ConsoleLog.h"
#include "paxx/BuildConfig.h"
#include "pt/pt_display.h"
#include <WiFi.h>

void PaxxApp::begin() {
    PaxxPreferences::instance().load(config_);

#if PAXX_REMOTE_ONLY
    config_.remoteScreenEnabled = true;
#endif

    PaxxTheme::apply(config_.darkTheme);
    buildShell();
    PaxxNotify::init(shell_);

#ifndef PAXX_REMOTE_ONLY
    moonraker_.setStatusCallback([this](const PrinterStatus &s) { onStatusUpdate(s); });
    moonraker_.setNotifyCallback([this](const char *title, const char *msg) {
        if (config_.notificationsEnabled) PaxxNotify::show(title, msg);
    });
#endif
    wifi_.setStatusCallback([this](bool connected, const char *message) {
        if (activeScreen_ == wifiScreen_.root() && message && message[0]) {
            wifiScreen_.setStatus(message);
        }
        if (connected) {
            wifiLostAtMs_ = 0;
            if (PaxxPreferences::instance().hasPrinter()) applyProfile();
#if PAXX_REMOTE_ONLY
            if (activeTickKind_ && strcmp(activeTickKind_, "remote") == 0) {
                syncServices();
                remoteScreen_.resetProbe();
            }
#endif
        } else if (!wifi_.isConnectPending()) {
            wifiLostAtMs_ = millis();
        }
    });

    if (PaxxPreferences::instance().hasWifi()) {
        wifi_.startConnect(config_.wifi.ssid, config_.wifi.password, 15);
    }

    if (PaxxPreferences::instance().hasPrinter() && WiFi.isConnected()) {
        applyProfile();
    }

    if (!PaxxPreferences::instance().hasPrinter()) {
        if (!PaxxPreferences::instance().hasWifi()) {
            showWifi();
        } else {
            showSetup();
        }
#if PAXX_REMOTE_ONLY
    } else {
        showRemote();
#endif
    }

    ota_.begin("paxxtouch");
}

void PaxxApp::syncServices() {
    const PrinterProfile &p = activeProfile(config_);
    const char *apiKey = p.apiKey[0] ? p.apiKey : nullptr;
    const char *token = moonrakerRest_.authToken()[0] ? moonrakerRest_.authToken() : nullptr;
    remoteScreen_.begin(p.host, p.useAuth, p.username, p.password, apiKey, token);
#if PAXX_REMOTE_ONLY
    remoteScreen_.setEnabled(true);
#else
    remoteScreen_.setEnabled(config_.remoteScreenEnabled);
    camera_.begin(p.host, p.useAuth, p.username, p.password, apiKey, token);
    thumbnails_.begin(p.host, p.moonrakerPort, p.useAuth, p.username, p.password, p.apiKey);
#endif
}

void PaxxApp::ensureMoonrakerRest() {
    const PrinterProfile &p = activeProfile(config_);
    if (p.host[0] == '\0') return;
    moonrakerRest_.configure(p.host, p.moonrakerPort, p.useAuth, p.username, p.password, p.apiKey);
    String token;
    moonrakerRest_.login(token);
    syncServices();
}

bool PaxxApp::sendGcode(const char *script) {
    if (!script || !script[0]) return false;
    paxx_log("> %s", script);
    if (moonraker_.connectionState() == ConnectionState::Connected) {
        moonraker_.sendGcode(script);
        return true;
    }
    ensureMoonrakerRest();
    return moonrakerRest_.sendGcodeScript(script);
}

void PaxxApp::applyProfile() {
    if (!WiFi.isConnected()) return;

    const PrinterProfile &p = activeProfile(config_);
    if (p.host[0] == '\0') return;

#if PAXX_REMOTE_ONLY
    Serial.printf("[Remote] apply profile host=%s\n", p.host);
    moonrakerRest_.configure(p.host, p.moonrakerPort, p.useAuth, p.username, p.password, p.apiKey);
    syncServices();

    if (p.useAuth && !p.apiKey[0]) {
        String token;
        if (moonrakerRest_.login(token)) {
            syncServices();
            if (remoteScreen_.isViewActive()) remoteScreen_.resetProbe();
        } else {
            Serial.println("[Remote] login failed — check username/password");
        }
    }
    return;
#endif

    if (moonraker_.isConnectedTo(p.host, p.moonrakerPort)) {
        syncServices();
        return;
    }

    const ConnectionState cs = moonraker_.connectionState();
    if (moonraker_.isLinkedTo(p.host, p.moonrakerPort) && cs == ConnectionState::Connecting) {
        return;
    }

    Serial.printf("[Moonraker] apply profile host=%s port=%u\n", p.host, p.moonrakerPort);

    moonrakerRest_.configure(p.host, p.moonrakerPort, p.useAuth, p.username, p.password, p.apiKey);
    syncServices();

    if (!moonrakerRest_.pingServer()) {
        Serial.println("[Moonraker] REST unreachable — will retry; remote/camera use port 80");
    } else {
        Serial.println("[Moonraker] REST ok");
    }

    String token;
    if (!moonrakerRest_.login(token) && p.useAuth) {
        Serial.println("[Moonraker] login failed — check username/password or API key");
    }
    syncServices();

    const char *tok = token.length() ? token.c_str() : (p.apiKey[0] ? p.apiKey : nullptr);
    const char *key = p.apiKey[0] ? p.apiKey : nullptr;

    if (!moonraker_.isLinkedTo(p.host, p.moonrakerPort)) {
        moonraker_.disconnect();
        moonraker_.begin(p.host, p.moonrakerPort, tok, key);
    }

    if (moonraker_.connectionState() != ConnectionState::Connected &&
        moonraker_.connectionState() != ConnectionState::Connecting) {
        moonraker_.connect();
    }

    syncServices();
}

void PaxxApp::saveConfig() {
    PaxxPreferences::instance().save(config_);
}

void PaxxApp::reconnectPrinter() {
    applyProfile();
}

void PaxxApp::loop() {
    pt_loop_display();
#ifndef PAXX_REMOTE_ONLY
    moonraker_.loop();
#endif
    wifi_.loop();

    if (wifiLostAtMs_ != 0 && !WiFi.isConnected() &&
        millis() - wifiLostAtMs_ > 5000) {
#ifndef PAXX_REMOTE_ONLY
        moonraker_.disconnect();
#endif
        wifiLostAtMs_ = 0;
    }

#if !PAXX_REMOTE_ONLY
    if (WiFi.isConnected() && activeProfile(config_).host[0] != '\0') {
        static unsigned long lastMoonrakerRetryMs = 0;
        static unsigned long lastRestPollMs = 0;
        const ConnectionState cs = moonraker_.connectionState();

        if (cs != ConnectionState::Connected && cs != ConnectionState::Connecting &&
            millis() - lastMoonrakerRetryMs > 10000) {
            lastMoonrakerRetryMs = millis();
            applyProfile();
        }

        if (cs != ConnectionState::Connecting && millis() - lastRestPollMs > 2000) {
            lastRestPollMs = millis();
            moonraker_.pollViaRest(moonrakerRest_);
        }
    }
#endif

    ota_.loop();
    PaxxNotify::loop();

#ifndef PAXX_REMOTE_ONLY
    if (activeScreen_ == home_.root()) home_.onTick();
#endif

    if (activeTickKind_) {
        if (strcmp(activeTickKind_, "remote") == 0) remote_.onTick();
#ifndef PAXX_REMOTE_ONLY
        else if (strcmp(activeTickKind_, "camera") == 0) cameraScreen_.onTick();
        else if (strcmp(activeTickKind_, "terminal") == 0) terminal_.onTick();
#endif
    }
}

lv_obj_t *PaxxApp::createMenuButton(lv_obj_t *parent, const char *icon, const char *label, lv_event_cb_t cb) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 210, 68);
    lv_obj_set_style_bg_color(btn, PaxxTheme::surface(isDark()), LV_PART_MAIN);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, this);

    lv_obj_t *row = lv_obj_create(btn);
    lv_obj_set_size(row, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, LV_PART_MAIN);
    paxx_disable_input(row);

    lv_obj_t *ico = lv_label_create(row);
    lv_label_set_text(ico, icon);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label);
    return btn;
}

void PaxxApp::hideGearMenu() {
    if (gearMenu_) lv_obj_add_flag(gearMenu_, LV_OBJ_FLAG_HIDDEN);
}

void PaxxApp::onKeyboardVisibility(bool visible, void *userData) {
    auto *app = static_cast<PaxxApp *>(userData);
    if (!app) return;

    if (visible) {
        app->hideGearMenu();
        if (app->gearBtn_) lv_obj_add_flag(app->gearBtn_, LV_OBJ_FLAG_HIDDEN);
    } else if (app->gearBtn_) {
        lv_obj_remove_flag(app->gearBtn_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(app->gearBtn_);
    }
}

void PaxxApp::toggleGearMenu() {
    if (!gearMenu_) return;
    if (lv_obj_has_flag(gearMenu_, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(gearMenu_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(gearMenu_);
        lv_obj_move_foreground(gearBtn_);
    } else {
        hideGearMenu();
    }
}

void PaxxApp::buildGearMenu() {
    gearBtn_ = lv_btn_create(shell_);
    lv_obj_set_size(gearBtn_, 40, 40);
    lv_obj_align(gearBtn_, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_set_style_bg_color(gearBtn_, PaxxTheme::surface(isDark()), LV_PART_MAIN);
    lv_obj_add_event_cb(gearBtn_, [](lv_event_t *e) {
        static_cast<PaxxApp *>(lv_event_get_user_data(e))->toggleGearMenu();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t *gearIcon = lv_label_create(gearBtn_);
    lv_label_set_text(gearIcon, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(gearIcon, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(gearIcon);

    gearMenu_ = lv_obj_create(shell_);
#if PAXX_REMOTE_ONLY
    lv_obj_set_size(gearMenu_, 220, 156);
#else
    lv_obj_set_size(gearMenu_, 220, 108);
#endif
    lv_obj_align(gearMenu_, LV_ALIGN_TOP_RIGHT, -8, 54);
    lv_obj_set_style_bg_color(gearMenu_, PaxxTheme::surface(isDark()), LV_PART_MAIN);
    lv_obj_set_style_border_color(gearMenu_, PaxxTheme::primary(), LV_PART_MAIN);
    lv_obj_set_style_border_width(gearMenu_, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(gearMenu_, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(gearMenu_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(gearMenu_, 8, LV_PART_MAIN);
    lv_obj_add_flag(gearMenu_, LV_OBJ_FLAG_HIDDEN);
    paxx_disable_input(gearMenu_);

    lv_obj_t *wifiBtn = lv_btn_create(gearMenu_);
    lv_obj_set_width(wifiBtn, LV_PCT(100));
    lv_obj_set_height(wifiBtn, 40);
    lv_obj_add_event_cb(wifiBtn, [](lv_event_t *e) {
        auto *app = static_cast<PaxxApp *>(lv_event_get_user_data(e));
        app->hideGearMenu();
        app->showWifi();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t *wifiLbl = lv_label_create(wifiBtn);
    lv_label_set_text(wifiLbl, LV_SYMBOL_WIFI "  WiFi Setup");
    lv_obj_center(wifiLbl);

    lv_obj_t *printerBtn = lv_btn_create(gearMenu_);
    lv_obj_set_width(printerBtn, LV_PCT(100));
    lv_obj_set_height(printerBtn, 40);
    lv_obj_add_event_cb(printerBtn, [](lv_event_t *e) {
        auto *app = static_cast<PaxxApp *>(lv_event_get_user_data(e));
        app->hideGearMenu();
        app->showSetup();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t *printerLbl = lv_label_create(printerBtn);
    lv_label_set_text(printerLbl, LV_SYMBOL_DRIVE "  Printer Connection");
    lv_obj_center(printerLbl);

#if PAXX_REMOTE_ONLY
    lv_obj_t *settingsBtn = lv_btn_create(gearMenu_);
    lv_obj_set_width(settingsBtn, LV_PCT(100));
    lv_obj_set_height(settingsBtn, 40);
    lv_obj_add_event_cb(settingsBtn, [](lv_event_t *e) {
        auto *app = static_cast<PaxxApp *>(lv_event_get_user_data(e));
        app->hideGearMenu();
        app->showSettings();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t *settingsLbl = lv_label_create(settingsBtn);
    lv_label_set_text(settingsLbl, LV_SYMBOL_LIST "  About / Settings");
    lv_obj_center(settingsLbl);

    lv_obj_t *remoteBtn = lv_btn_create(gearMenu_);
    lv_obj_set_width(remoteBtn, LV_PCT(100));
    lv_obj_set_height(remoteBtn, 40);
    lv_obj_add_event_cb(remoteBtn, [](lv_event_t *e) {
        auto *app = static_cast<PaxxApp *>(lv_event_get_user_data(e));
        app->hideGearMenu();
        app->showRemote();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t *remoteLbl = lv_label_create(remoteBtn);
    lv_label_set_text(remoteLbl, LV_SYMBOL_IMAGE "  Remote Screen");
    lv_obj_center(remoteLbl);
#endif

    lv_obj_move_foreground(gearBtn_);
}

void PaxxApp::buildShell() {
    shell_ = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(shell_, PaxxTheme::bg(isDark()), LV_PART_MAIN);
    paxx_disable_input(shell_);

    content_ = lv_obj_create(shell_);
    lv_obj_set_size(content_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(content_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(content_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(content_, 0, LV_PART_MAIN);
    paxx_disable_input(content_);

#if PAXX_REMOTE_ONLY
    remote_.create(this, content_);
    setup_.create(this, content_);
    wifiScreen_.create(this, content_);
    settings_.create(this, content_);
#else
    home_.create(this, content_);
    print_.create(this, content_);
    filament_.create(this, content_);
    remote_.create(this, content_);
    timelapse_.create(this, content_);
    cameraScreen_.create(this, content_);
    files_.create(this, content_);
    printPrepare_.create(this, content_);
    controls_.create(this, content_);
    terminal_.create(this, content_);
    settings_.create(this, content_);
    setup_.create(this, content_);
    wifiScreen_.create(this, content_);
#endif

    lv_scr_load(shell_);
    PaxxKeyboard::init(shell_);
    PaxxKeyboard::setVisibilityListener(onKeyboardVisibility, this);
    buildGearMenu();
#if PAXX_REMOTE_ONLY
    showRemote();
#else
    showHome();
#endif
}

void PaxxApp::showScreen(lv_obj_t *screen, const char *tickKind) {
    hideGearMenu();
    PaxxKeyboard::hide();
    if (activeTickKind_) {
        if (strcmp(activeTickKind_, "remote") == 0) remote_.onLeave();
#ifndef PAXX_REMOTE_ONLY
        else if (strcmp(activeTickKind_, "camera") == 0) cameraScreen_.onLeave();
#endif
    }

#if PAXX_REMOTE_ONLY
    lv_obj_add_flag(remote_.root(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(setup_.root(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(wifiScreen_.root(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(settings_.root(), LV_OBJ_FLAG_HIDDEN);
#else
    lv_obj_add_flag(home_.root(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(print_.root(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(filament_.root(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(remote_.root(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(timelapse_.root(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cameraScreen_.root(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(files_.root(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(printPrepare_.root(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(controls_.root(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(terminal_.root(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(settings_.root(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(setup_.root(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(wifiScreen_.root(), LV_OBJ_FLAG_HIDDEN);
#endif

    activeScreen_ = screen;
    activeTickKind_ = tickKind;
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);

    if (tickKind && strcmp(tickKind, "remote") == 0) remote_.onEnter();
#ifndef PAXX_REMOTE_ONLY
    else if (screen == timelapse_.root()) timelapse_.onEnter();
    else if (tickKind && strcmp(tickKind, "camera") == 0) cameraScreen_.onEnter();
    else if (screen == files_.root()) files_.onEnter();
    else if (screen == filament_.root()) filament_.onEnter();
#endif
    else if (screen == wifiScreen_.root()) wifiScreen_.onEnter();

#ifndef PAXX_REMOTE_ONLY
    refreshActiveScreen(moonraker_.status());
#endif
}

void PaxxApp::showHome() {
    showScreen(home_.root());
    const PrinterStatus &status = moonraker_.status();
    home_.update(status);
    home_.onTick();
}
void PaxxApp::showPrint() { showScreen(print_.root()); }
void PaxxApp::showFilament() { showScreen(filament_.root()); }
void PaxxApp::showRemote() { showScreen(remote_.root(), "remote"); }
void PaxxApp::showTimelapse() { showScreen(timelapse_.root()); }
void PaxxApp::showCamera() { showScreen(cameraScreen_.root(), "camera"); }
void PaxxApp::showFiles() { showScreen(files_.root()); }
void PaxxApp::showPrintPrepare(const char *gcodePath) {
    printPrepare_.open(gcodePath);
    showScreen(printPrepare_.root());
}
void PaxxApp::showControls() { showScreen(controls_.root()); }
void PaxxApp::showTerminal() { showScreen(terminal_.root(), "terminal"); }
void PaxxApp::showSettings() { showScreen(settings_.root()); }
void PaxxApp::showSetup() { showScreen(setup_.root()); }
void PaxxApp::showWifi() { showScreen(wifiScreen_.root()); }

void PaxxApp::refreshActiveScreen(const PrinterStatus &status) {
    if (activeScreen_ == home_.root()) home_.update(status);
    else if (activeScreen_ == print_.root()) print_.update(status);
    else if (activeScreen_ == filament_.root()) filament_.update(status);
    else if (activeScreen_ == controls_.root()) controls_.update(status);
}

void PaxxApp::onStatusUpdate(const PrinterStatus &status) {
    static unsigned long lastUiMs = 0;
    static ConnectionState lastConn = ConnectionState::Disconnected;
    static float lastProgress = -1.0f;
    static PrintState lastPrintState = PrintState::Unknown;

    const bool connChanged = status.connection != lastConn;
    const bool progressChanged = fabsf(status.progress - lastProgress) >= 0.5f;
    const bool stateChanged = status.printState != lastPrintState;

    if (!connChanged && !progressChanged && !stateChanged && millis() - lastUiMs < 400) return;

    lastConn = status.connection;
    lastProgress = status.progress;
    lastPrintState = status.printState;
    lastUiMs = millis();

    refreshActiveScreen(status);
}
