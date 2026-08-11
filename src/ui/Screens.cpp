#include "ui/App.h"
#include "ui/Theme.h"
#include "ui/Notify.h"
#include "ui/Keyboard.h"
#include "ui/ConsoleLog.h"
#include "paxx/BuildConfig.h"
#include "paxx/ImageDecoder.h"
#include "pt/pt_board.h"

#include <Arduino.h>
#include <WiFi.h>

namespace {

constexpr int kNavBarHeight = 48;

struct ImageFit {
    int offsetX = 0;
    int offsetY = 0;
    int dispW = 0;
    int dispH = 0;
};

ImageFit fitImageInArea(int imgW, int imgH, int areaW, int areaH) {
    ImageFit fit;
    if (imgW <= 0 || imgH <= 0 || areaW <= 0 || areaH <= 0) return fit;

    const int scaleX = (areaW * 256) / imgW;
    const int scaleY = (areaH * 256) / imgH;
    const int scale = scaleX < scaleY ? scaleX : scaleY;
    fit.dispW = (imgW * scale) / 256;
    fit.dispH = (imgH * scale) / 256;
    fit.offsetX = (areaW - fit.dispW) / 2;
    fit.offsetY = (areaH - fit.dispH) / 2;
    return fit;
}

uint32_t parseHexColor(const char *hex, uint32_t fallback = 0x888888) {
    if (!hex || !hex[0]) return fallback;
    const char *p = hex[0] == '#' ? hex + 1 : hex;
    char buf[7] = {};
    strlcpy(buf, p, sizeof(buf));
    return strtoul(buf, nullptr, 16);
}

lv_color_t hexToLvColor(const char *hex) {
    const uint32_t rgb = parseHexColor(hex);
    return lv_color_hex(rgb);
}

uint32_t colorDistance(const char *a, const char *b) {
    const uint32_t ca = parseHexColor(a);
    const uint32_t cb = parseHexColor(b);
    const int dr = static_cast<int>((ca >> 16) & 0xFF) - static_cast<int>((cb >> 16) & 0xFF);
    const int dg = static_cast<int>((ca >> 8) & 0xFF) - static_cast<int>((cb >> 8) & 0xFF);
    const int db = static_cast<int>(ca & 0xFF) - static_cast<int>(cb & 0xFF);
    return static_cast<uint32_t>(dr * dr + dg * dg + db * db);
}

int bestToolForPrintColor(const char *printHex, const PrinterStatus &status, int defaultTool) {
    if (status.filaments.empty()) return defaultTool;

    int bestTool = defaultTool;
    uint32_t bestDist = UINT32_MAX;
    for (const FilamentSlot &f : status.filaments) {
        const uint32_t d = colorDistance(printHex, f.color);
        if (d < bestDist) {
            bestDist = d;
            bestTool = f.index;
        }
    }
    return bestTool;
}

const char *connectionLabel(ConnectionState s) {
    switch (s) {
        case ConnectionState::Connected: return "Connected";
        case ConnectionState::Connecting: return "Connecting";
        case ConnectionState::Error: return "Error";
        default: return "Offline";
    }
}

const char *printStateLabel(PrintState s) {
    switch (s) {
        case PrintState::Printing: return "Printing";
        case PrintState::Paused: return "Paused";
        case PrintState::Complete: return "Complete";
        case PrintState::Cancelled: return "Cancelled";
        case PrintState::Error: return "Error";
        case PrintState::Standby: return "Standby";
        default: return "Idle";
    }
}

bool isActivePrint(PrintState s) {
    return s == PrintState::Printing || s == PrintState::Paused;
}

const char *basenameOnly(const char *path) {
    if (!path || !path[0]) return "(none)";
    const char *base = strrchr(path, '/');
    return base ? base + 1 : path;
}

lv_color_t connectionColor(ConnectionState s) {
    switch (s) {
        case ConnectionState::Connected: return PaxxTheme::accent();
        case ConnectionState::Connecting: return PaxxTheme::warn();
        default: return PaxxTheme::danger();
    }
}

lv_obj_t *makeMenuBtn(PaxxApp *app, lv_obj_t *parent, const char *icon, const char *label, lv_event_cb_t cb) {
    return app->createMenuButton(parent, icon, label, cb);
}

}  // namespace

void paxx_back_home_cb(lv_event_t *e) {
#if PAXX_REMOTE_ONLY
    static_cast<PaxxApp *>(lv_event_get_user_data(e))->showRemote();
#else
    static_cast<PaxxApp *>(lv_event_get_user_data(e))->showHome();
#endif
}

void paxx_back_remote_cb(lv_event_t *e) {
    static_cast<PaxxApp *>(lv_event_get_user_data(e))->showRemote();
}

void paxx_back_files_cb(lv_event_t *e) {
    static_cast<PaxxApp *>(lv_event_get_user_data(e))->showFiles();
}

void HomeScreen::create(PaxxApp *app, lv_obj_t *parent) {
    app_ = app;
    const bool dark = app->isDark();
    screen_ = lv_obj_create(parent);
    lv_obj_set_size(screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(screen_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_, 0, LV_PART_MAIN);
    paxx_disable_input(screen_);
    lv_obj_set_flex_flow(screen_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(screen_, 10, LV_PART_MAIN);

    lv_obj_t *header = lv_obj_create(screen_);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, 40);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
    paxx_disable_input(header);
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "PaxxTouch");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, LV_PART_MAIN);
    connChip_ = paxx_create_status_chip(header, "Offline", PaxxTheme::danger());
    lv_obj_align(connChip_, LV_ALIGN_RIGHT_MID, -48, 0);

    lv_obj_t *card = lv_obj_create(screen_);
    card_ = card;
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, 150);
    lv_obj_set_style_bg_color(card, PaxxTheme::surface(dark), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 6, LV_PART_MAIN);
    paxx_disable_input(card);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(card, 10, LV_PART_MAIN);

    previewFrame_ = lv_obj_create(card);
    lv_obj_set_size(previewFrame_, 100, 100);
    lv_obj_set_style_bg_color(previewFrame_, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(previewFrame_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(previewFrame_, 0, LV_PART_MAIN);
    lv_obj_add_flag(previewFrame_, LV_OBJ_FLAG_HIDDEN);
    previewImage_ = lv_image_create(previewFrame_);
    lv_obj_center(previewImage_);

    infoCol_ = lv_obj_create(card);
    lv_obj_set_flex_grow(infoCol_, 1);
    lv_obj_set_height(infoCol_, LV_PCT(100));
    lv_obj_set_style_bg_opa(infoCol_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(infoCol_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(infoCol_, 0, LV_PART_MAIN);
    paxx_disable_input(infoCol_);

    stateLbl_ = lv_label_create(infoCol_);
    lv_label_set_text(stateLbl_, "");
    lv_obj_set_style_text_color(stateLbl_, PaxxTheme::accent(), LV_PART_MAIN);
    filenameLbl_ = lv_label_create(infoCol_);
    lv_label_set_text(filenameLbl_, "No active print");
    lv_obj_set_width(filenameLbl_, LV_PCT(100));
    lv_label_set_long_mode(filenameLbl_, LV_LABEL_LONG_DOT);
    progressBar_ = lv_bar_create(infoCol_);
    lv_obj_set_width(progressBar_, LV_PCT(100));
    lv_obj_align(progressBar_, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_bar_set_range(progressBar_, 0, 100);
    progressLbl_ = lv_label_create(infoCol_);
    lv_label_set_text(progressLbl_, "0%");
    lv_obj_align(progressLbl_, LV_ALIGN_BOTTOM_LEFT, 0, -2);
    tempLbl_ = lv_label_create(infoCol_);
    lv_label_set_text(tempLbl_, "Nozzle --  Bed --");
    lv_obj_align(tempLbl_, LV_ALIGN_BOTTOM_RIGHT, 0, -2);

    lv_obj_t *menu = lv_obj_create(screen_);
    lv_obj_set_width(menu, LV_PCT(100));
    lv_obj_set_flex_grow(menu, 1);
    lv_obj_set_style_bg_opa(menu, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(menu, 0, LV_PART_MAIN);
    paxx_disable_input(menu);
    lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(menu, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    makeMenuBtn(app, menu, LV_SYMBOL_PLAY, "Print", [](lv_event_t *e) { static_cast<PaxxApp *>(lv_event_get_user_data(e))->showFiles(); });
    makeMenuBtn(app, menu, LV_SYMBOL_TINT, "Filament", [](lv_event_t *e) { static_cast<PaxxApp *>(lv_event_get_user_data(e))->showFilament(); });
    makeMenuBtn(app, menu, LV_SYMBOL_IMAGE, "Remote", [](lv_event_t *e) { static_cast<PaxxApp *>(lv_event_get_user_data(e))->showRemote(); });
    makeMenuBtn(app, menu, LV_SYMBOL_VIDEO, "Timelapse", [](lv_event_t *e) { static_cast<PaxxApp *>(lv_event_get_user_data(e))->showTimelapse(); });
    makeMenuBtn(app, menu, LV_SYMBOL_EYE_OPEN, "Camera", [](lv_event_t *e) { static_cast<PaxxApp *>(lv_event_get_user_data(e))->showCamera(); });
    makeMenuBtn(app, menu, LV_SYMBOL_LIST, "Files", [](lv_event_t *e) { static_cast<PaxxApp *>(lv_event_get_user_data(e))->showFiles(); });
    makeMenuBtn(app, menu, LV_SYMBOL_EDIT, "Terminal", [](lv_event_t *e) { static_cast<PaxxApp *>(lv_event_get_user_data(e))->showTerminal(); });
    makeMenuBtn(app, menu, LV_SYMBOL_SHUFFLE, "Controls", [](lv_event_t *e) { static_cast<PaxxApp *>(lv_event_get_user_data(e))->showControls(); });
    makeMenuBtn(app, menu, LV_SYMBOL_SETTINGS, "Settings", [](lv_event_t *e) { static_cast<PaxxApp *>(lv_event_get_user_data(e))->showSettings(); });
}

void HomeScreen::update(const PrinterStatus &status) {
    if (!connChip_) return;

    if (status.connection != lastConnection_) {
        lastConnection_ = status.connection;
        const lv_color_t color = connectionColor(status.connection);
        lv_label_set_text(lv_obj_get_child(connChip_, 0), connectionLabel(status.connection));
        lv_obj_set_style_bg_color(connChip_, color, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(connChip_, LV_OPA_30, LV_PART_MAIN);
        lv_obj_set_style_text_color(lv_obj_get_child(connChip_, 0), color, LV_PART_MAIN);
    }

    if (status.printState != lastPrintState_) {
        lastPrintState_ = status.printState;
        if (isActivePrint(status.printState)) {
            lv_label_set_text(stateLbl_, printStateLabel(status.printState));
            lv_obj_clear_flag(stateLbl_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(stateLbl_, "");
            lv_obj_add_flag(stateLbl_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    const char *displayName = isActivePrint(status.printState) && status.filename[0]
                                  ? basenameOnly(status.filename)
                                  : "No active print";
    if (strcmp(displayName, lastFilename_) != 0) {
        strlcpy(lastFilename_, displayName, sizeof(lastFilename_));
        lv_label_set_text(filenameLbl_, lastFilename_);
    }

    const int progress = isActivePrint(status.printState)
                             ? static_cast<int>(status.progress + 0.5f)
                             : 0;
    if (progress != lastProgress_) {
        lastProgress_ = progress;
        lv_bar_set_value(progressBar_, progress, LV_ANIM_OFF);
        lv_label_set_text_fmt(progressLbl_, "%d%%", progress);
    }

    const int nozzleTemp = static_cast<int>(status.nozzleTemp + 0.5f);
    const int nozzleTarget = static_cast<int>(status.nozzleTarget + 0.5f);
    const int bedTemp = static_cast<int>(status.bedTemp + 0.5f);
    const int bedTarget = static_cast<int>(status.bedTarget + 0.5f);
    if (status.activeTool != lastActiveTool_ || nozzleTemp != lastNozzleTemp_ ||
        nozzleTarget != lastNozzleTarget_ || bedTemp != lastBedTemp_ ||
        bedTarget != lastBedTarget_) {
        lastActiveTool_ = status.activeTool;
        lastNozzleTemp_ = nozzleTemp;
        lastNozzleTarget_ = nozzleTarget;
        lastBedTemp_ = bedTemp;
        lastBedTarget_ = bedTarget;
        lv_label_set_text_fmt(tempLbl_, "T%d %d/%d  Bed %d/%d",
                              status.activeTool, nozzleTemp, nozzleTarget, bedTemp, bedTarget);
    }

    syncPreview(status);
}

void HomeScreen::releasePreview() {
    if (previewImage_) lv_image_set_src(previewImage_, NULL);
    if (previewBuf_) {
        ImageDecoder::freeBuffer(previewBuf_);
        previewBuf_ = nullptr;
    }
    previewFormat_ = LV_COLOR_FORMAT_UNKNOWN;
}

void HomeScreen::syncPreview(const PrinterStatus &status) {
    if (!app_) return;

    const bool active = isActivePrint(status.printState) && status.filename[0];
    if (!active) {
        if (previewFrame_) lv_obj_add_flag(previewFrame_, LV_OBJ_FLAG_HIDDEN);
        if (strcmp(lastThumbRequest_, "") != 0 || lastShownThumb_[0]) {
            app_->thumbnails().clear();
            lastThumbRequest_[0] = '\0';
            lastShownThumb_[0] = '\0';
            releasePreview();
        }
        return;
    }

    if (strcmp(status.filename, lastThumbRequest_) != 0) {
        strlcpy(lastThumbRequest_, status.filename, sizeof(lastThumbRequest_));
        lastShownThumb_[0] = '\0';
        releasePreview();
        app_->ensureMoonrakerRest();
        app_->thumbnails().request(status.filename);
    }

    if (previewFrame_ && lastShownThumb_[0] && strcmp(lastShownThumb_, status.filename) == 0) {
        lv_obj_clear_flag(previewFrame_, LV_OBJ_FLAG_HIDDEN);
    }
}

void HomeScreen::applyReadyPreview() {
    if (!app_ || !previewImage_) return;

    void *pixels = nullptr;
    lv_color_format_t format = LV_COLOR_FORMAT_UNKNOWN;
    int w = 0;
    int h = 0;
    char path[96] = {};
    if (!app_->thumbnails().poll(pixels, format, w, h, path, sizeof(path))) return;
    if (!pixels || w <= 0 || h <= 0) return;

    releasePreview();
    previewBuf_ = pixels;
    previewFormat_ = format;
    strlcpy(lastShownThumb_, path, sizeof(lastShownThumb_));
    ImageDecoder::bindLvImage(previewDsc_, previewImage_, previewBuf_, previewFormat_, w, h, 100, 100);
    if (previewFrame_) lv_obj_clear_flag(previewFrame_, LV_OBJ_FLAG_HIDDEN);
}

void HomeScreen::onTick() {
    applyReadyPreview();
}

void PrintScreen::create(PaxxApp *app, lv_obj_t *parent) {
    app_ = app;
    screen_ = lv_obj_create(parent);
    lv_obj_set_size(screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(screen_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_, 0, LV_PART_MAIN);
    paxx_create_nav_bar(screen_, "Print Job", paxx_back_home_cb, app, app->isDark());

    lv_obj_t *browse = lv_btn_create(screen_);
    lv_obj_align(browse, LV_ALIGN_TOP_RIGHT, -8, 52);
    lv_obj_add_event_cb(browse, [](lv_event_t *e) {
        static_cast<PaxxApp *>(lv_event_get_user_data(e))->showFiles();
    }, LV_EVENT_CLICKED, app);
    lv_label_set_text(lv_label_create(browse), LV_SYMBOL_LIST " Files");

    detailsLbl_ = lv_label_create(screen_);
    lv_obj_align(detailsLbl_, LV_ALIGN_TOP_LEFT, 8, 88);
    lv_obj_set_width(detailsLbl_, LV_PCT(95));
    lv_label_set_long_mode(detailsLbl_, LV_LABEL_LONG_WRAP);

    messageLbl_ = lv_label_create(screen_);
    lv_obj_align(messageLbl_, LV_ALIGN_TOP_LEFT, 8, 200);
    lv_obj_set_width(messageLbl_, LV_PCT(95));
    lv_label_set_long_mode(messageLbl_, LV_LABEL_LONG_WRAP);

    lv_obj_t *row = lv_obj_create(screen_);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    paxx_disable_input(row);
    lv_obj_set_style_pad_column(row, 10, LV_PART_MAIN);

    lv_obj_t *pause = lv_btn_create(row);
    lv_obj_add_event_cb(pause, [](lv_event_t *e) { static_cast<PaxxApp *>(lv_event_get_user_data(e))->moonraker().pausePrint(); }, LV_EVENT_CLICKED, app);
    lv_label_set_text(lv_label_create(pause), "Pause");
    lv_obj_t *resume = lv_btn_create(row);
    lv_obj_add_event_cb(resume, [](lv_event_t *e) { static_cast<PaxxApp *>(lv_event_get_user_data(e))->moonraker().resumePrint(); }, LV_EVENT_CLICKED, app);
    lv_label_set_text(lv_label_create(resume), "Resume");
    lv_obj_t *cancel = lv_btn_create(row);
    lv_obj_set_style_bg_color(cancel, PaxxTheme::danger(), LV_PART_MAIN);
    lv_obj_add_event_cb(cancel, [](lv_event_t *e) { static_cast<PaxxApp *>(lv_event_get_user_data(e))->moonraker().cancelPrint(); }, LV_EVENT_CLICKED, app);
    lv_label_set_text(lv_label_create(cancel), "Cancel");
}

void PrintScreen::update(const PrinterStatus &status) {
    if (!detailsLbl_) return;
    lv_label_set_text_fmt(detailsLbl_,
        "File: %s\nProgress: %d%%\nDuration: %.0fs / %.0fs\nTool: T%d\nSpeed: %.0f%%  Flow: %.0f%%\n"
        "T0 %.0f  T1 %.0f  T2 %.0f  T3 %.0f",
        status.filename[0] ? status.filename : "(none)", static_cast<int>(status.progress),
        status.printDuration, status.totalDuration, status.activeTool,
        status.speedFactor, status.flowFactor,
        status.extruderTemps[0], status.extruderTemps[1], status.extruderTemps[2], status.extruderTemps[3]);
    lv_label_set_text(messageLbl_, status.stateMessage[0] ? status.stateMessage : "");
}

void FilamentScreen::setSelectedColor(const char *hex) {
    if (!hex || !hex[0]) return;
    if (hex[0] == '#') strlcpy(selectedColor_, hex, sizeof(selectedColor_));
    else snprintf(selectedColor_, sizeof(selectedColor_), "#%.6s", hex);
    updateColorPreview();
}

void FilamentScreen::updateColorPreview() {
    if (!colorSwatch_) return;
    lv_obj_set_style_bg_color(colorSwatch_, hexToLvColor(selectedColor_), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(colorSwatch_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(colorSwatch_, PaxxTheme::muted(app_->isDark()), LV_PART_MAIN);
    lv_obj_set_style_border_width(colorSwatch_, 2, LV_PART_MAIN);
}

void FilamentScreen::buildColorPicker() {
    if (!screen_) return;

    colorSwatch_ = lv_obj_create(screen_);
    lv_obj_set_size(colorSwatch_, 40, 40);
    lv_obj_align(colorSwatch_, LV_ALIGN_BOTTOM_LEFT, 8, -118);
    lv_obj_set_style_radius(colorSwatch_, 6, LV_PART_MAIN);
    updateColorPreview();

    colorGrid_ = lv_obj_create(screen_);
    lv_obj_align(colorGrid_, LV_ALIGN_BOTTOM_LEFT, 56, -130);
    lv_obj_set_size(colorGrid_, 420, 56);
    lv_obj_set_style_bg_opa(colorGrid_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(colorGrid_, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(colorGrid_, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(colorGrid_, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_row(colorGrid_, 4, LV_PART_MAIN);

    static const char *kPalette[] = {
        "#FF0000", "#00FF00", "#0000FF", "#FFFF00", "#FF8800", "#FF00FF",
        "#00FFFF", "#FFFFFF", "#000000", "#808080", "#F8F81C", "#080A0D",
        "#6F4C2F", "#E72F1D", "#FFC0CB", "#8B4513", "#4B0082", "#228B22",
        "#1E90FF", "#FFD700", "#C0C0C0", "#800000", "#008080", "#A0522D",
    };

    for (const char *hex : kPalette) {
        lv_obj_t *btn = lv_btn_create(colorGrid_);
        lv_obj_set_size(btn, 28, 28);
        lv_obj_set_style_bg_color(btn, hexToLvColor(hex), LV_PART_MAIN);
        lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
        lv_obj_set_user_data(btn, const_cast<char *>(hex));
        lv_obj_add_event_cb(btn, [](lv_event_t *e) {
            auto *self = static_cast<FilamentScreen *>(lv_event_get_user_data(e));
            const char *hex = static_cast<const char *>(
                lv_obj_get_user_data(static_cast<lv_obj_t *>(lv_event_get_target(e))));
            self->setSelectedColor(hex);
        }, LV_EVENT_CLICKED, this);
    }
}

void FilamentScreen::setHint(const char *text) {
    if (hintLbl_) lv_label_set_text(hintLbl_, text ? text : "");
}

bool FilamentScreen::filamentsChanged(const std::vector<FilamentSlot> &filaments) const {
    if (filaments.size() != lastFilaments_.size()) return true;
    for (size_t i = 0; i < filaments.size(); ++i) {
        const FilamentSlot &a = filaments[i];
        const FilamentSlot &b = lastFilaments_[i];
        if (a.index != b.index || a.loaded != b.loaded || a.mappedTool != b.mappedTool ||
            strcmp(a.material, b.material) != 0 || strcmp(a.color, b.color) != 0) {
            return true;
        }
    }
    return false;
}

void FilamentScreen::rebuildGrid(const PrinterStatus &status) {
    if (!grid_) return;
    lv_obj_clean(grid_);

    auto addSlotCard = [&](int index, bool loaded, int mappedTool, const char *material, const char *color) {
        lv_obj_t *card = lv_btn_create(grid_);
        lv_obj_set_size(card, 180, 88);

        lv_obj_t *row = lv_obj_create(card);
        lv_obj_set_size(row, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 8, LV_PART_MAIN);
        paxx_disable_input(row);

        lv_obj_t *swatch = lv_obj_create(row);
        lv_obj_set_size(swatch, 28, 28);
        lv_obj_set_style_bg_color(swatch, hexToLvColor(color), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(swatch, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(swatch, 4, LV_PART_MAIN);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text_fmt(lbl, "T%d  %s\n%s\nmap T%d",
                              index, loaded ? "present" : "empty",
                              material && material[0] ? material : "(none)", mappedTool);
        lv_obj_set_user_data(card, reinterpret_cast<void *>(static_cast<intptr_t>(index)));
        lv_obj_add_event_cb(card, [](lv_event_t *e) {
            auto *self = static_cast<FilamentScreen *>(lv_event_get_user_data(e));
            const int tool = static_cast<int>(reinterpret_cast<intptr_t>(
                lv_obj_get_user_data(static_cast<lv_obj_t *>(lv_event_get_target(e)))));
            self->editSlot_ = tool;
            self->app_->moonraker().setActiveTool(tool);
            for (const FilamentSlot &f : self->lastFilaments_) {
                if (f.index != tool) continue;
                lv_textarea_set_text(self->materialTa_, f.material);
                self->setSelectedColor(f.color);
                break;
            }
            char buf[32];
            snprintf(buf, sizeof(buf), "Editing T%d — pick color swatch below", tool);
            self->setHint(buf);
        }, LV_EVENT_CLICKED, this);
    };

    if (status.filaments.empty()) {
        static const char *kDefaultColors[] = {"#FF0000", "#00FF00", "#0000FF", "#FFFF00"};
        lastFilaments_.clear();
        for (int i = 0; i < 4; ++i) {
            FilamentSlot f{};
            f.index = i;
            f.mappedTool = i;
            snprintf(f.material, sizeof(f.material), "Slot %d", i);
            strlcpy(f.color, kDefaultColors[i], sizeof(f.color));
            lastFilaments_.push_back(f);
            addSlotCard(i, false, i, f.material, f.color);
        }
        setHint("No live filament data — showing T0–T3 defaults.");
        return;
    }

    for (const FilamentSlot &f : status.filaments) {
        addSlotCard(f.index, f.loaded, f.mappedTool, f.material, f.color);
    }
}

void FilamentScreen::onEnter() {
    lastFilaments_.clear();
    rebuildGrid(app_->moonraker().status());
}

void FilamentScreen::update(const PrinterStatus &status) {
    if (!grid_) return;
    if (!filamentsChanged(status.filaments)) return;
    lastFilaments_ = status.filaments;
    rebuildGrid(status);
}

void FilamentScreen::create(PaxxApp *app, lv_obj_t *parent) {
    app_ = app;
    screen_ = lv_obj_create(parent);
    lv_obj_set_size(screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(screen_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_, 0, LV_PART_MAIN);
    paxx_create_nav_bar(screen_, "Filament", paxx_back_home_cb, app, app->isDark());

    grid_ = lv_obj_create(screen_);
    lv_obj_align(grid_, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_size(grid_, LV_PCT(96), 150);
    lv_obj_set_style_bg_opa(grid_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(grid_, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(grid_, LV_FLEX_FLOW_ROW_WRAP);

    materialTa_ = lv_textarea_create(screen_);
    lv_obj_set_width(materialTa_, 180);
    lv_obj_align(materialTa_, LV_ALIGN_BOTTOM_LEFT, 8, -80);
    lv_textarea_set_placeholder_text(materialTa_, "Material");
    PaxxKeyboard::attach(materialTa_);

    buildColorPicker();

    lv_obj_t *saveBtn = lv_btn_create(screen_);
    lv_obj_align(saveBtn, LV_ALIGN_BOTTOM_RIGHT, -8, -80);
    lv_obj_add_event_cb(saveBtn, [](lv_event_t *e) {
        auto *a = static_cast<PaxxApp *>(lv_event_get_user_data(e));
        a->ensureMoonrakerRest();
        if (a->moonrakerRest().setFilamentSlot(a->filament().editSlot(),
                lv_textarea_get_text(a->filament().materialInput()),
                a->filament().selectedColor())) {
            a->filament().setHint("Saved via Paxx filament API");
            PaxxNotify::show("Filament", "Slot updated");
        } else {
            a->filament().setHint("Save failed — check Moonraker connection");
        }
    }, LV_EVENT_CLICKED, app);
    lv_label_set_text(lv_label_create(saveBtn), "Save");

    hintLbl_ = lv_label_create(screen_);
    lv_obj_align(hintLbl_, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_width(hintLbl_, LV_PCT(95));
    lv_label_set_long_mode(hintLbl_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(hintLbl_, "Tap slot to select toolhead.");
    lastFilaments_.clear();
}

void RemoteScreenView::create(PaxxApp *app, lv_obj_t *parent) {
    app_ = app;
    screen_ = lv_obj_create(parent);
    lv_obj_set_size(screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(screen_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_, 0, LV_PART_MAIN);
    paxx_disable_input(screen_);
#if !PAXX_REMOTE_ONLY
    paxx_create_nav_bar(screen_, "Remote Screen", paxx_back_home_cb, app, app->isDark());
#endif

    canvasArea_ = lv_obj_create(screen_);
#if PAXX_REMOTE_ONLY
    lv_obj_set_size(canvasArea_, LV_PCT(100), LV_PCT(100));
#else
    lv_obj_align(canvasArea_, LV_ALIGN_TOP_LEFT, 0, kNavBarHeight);
    lv_obj_set_size(canvasArea_, PT_LCD_H_RES, PT_LCD_V_RES - kNavBarHeight);
#endif
    lv_obj_set_style_bg_color(canvasArea_, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(canvasArea_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(canvasArea_, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(canvasArea_, 0, LV_PART_MAIN);
    lv_obj_remove_flag(canvasArea_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(canvasArea_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(canvasArea_, [](lv_event_t *e) {
        static_cast<RemoteScreenView *>(lv_event_get_user_data(e))->handleCanvasTouch(e);
    }, LV_EVENT_ALL, this);

    statusLbl_ = lv_label_create(canvasArea_);
    lv_obj_align(statusLbl_, LV_ALIGN_CENTER, 0, 32);
    lv_obj_set_width(statusLbl_, LV_PCT(95));
    lv_label_set_long_mode(statusLbl_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(statusLbl_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    paxx_disable_input(statusLbl_);

    loadingArc_ = paxx_create_loading_arc(canvasArea_);
    lv_obj_align(loadingArc_, LV_ALIGN_CENTER, 0, -24);

    touchSpinner_ = lv_arc_create(canvasArea_);
    lv_obj_set_size(touchSpinner_, 36, 36);
    lv_arc_set_rotation(touchSpinner_, 270);
    lv_arc_set_bg_angles(touchSpinner_, 0, 360);
    lv_arc_set_angles(touchSpinner_, 0, 90);
    lv_obj_set_style_arc_width(touchSpinner_, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(touchSpinner_, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_color(touchSpinner_, lv_color_hex(0x4DA3FF), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(touchSpinner_, lv_color_hex(0x303030), LV_PART_MAIN);
    lv_obj_remove_style(touchSpinner_, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(touchSpinner_, LV_OBJ_FLAG_CLICKABLE);
    paxx_disable_input(touchSpinner_);
    lv_obj_add_flag(touchSpinner_, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t touchAnim;
    lv_anim_init(&touchAnim);
    lv_anim_set_var(&touchAnim, touchSpinner_);
    lv_anim_set_exec_cb(&touchAnim, paxx_spinner_anim);
    lv_anim_set_values(&touchAnim, 30, 390);
    lv_anim_set_time(&touchAnim, 600);
    lv_anim_set_repeat_count(&touchAnim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&touchAnim);

    image_ = lv_image_create(canvasArea_);
    lv_obj_center(image_);
    paxx_disable_input(image_);
}

namespace {

bool mapRemoteTouchToU1(lv_obj_t *canvasArea, int frameW, int frameH, lv_point_t pt, int &u1x, int &u1y) {
    if (!canvasArea || frameW <= 0 || frameH <= 0) return false;

    lv_area_t canvas{};
    lv_obj_get_coords(canvasArea, &canvas);
    const int canvasW = lv_area_get_width(&canvas);
    const int canvasH = lv_area_get_height(&canvas);
    const ImageFit fit = fitImageInArea(frameW, frameH, canvasW, canvasH);
    if (fit.dispW <= 0 || fit.dispH <= 0) return false;

    const int localX = pt.x - canvas.x1 - fit.offsetX;
    const int localY = pt.y - canvas.y1 - fit.offsetY;
    if (localX < 0 || localY < 0 || localX >= fit.dispW || localY >= fit.dispH) return false;

    u1x = constrain((localX * frameW) / fit.dispW, 0, frameW - 1);
    u1y = constrain((localY * frameH) / fit.dispH, 0, frameH - 1);
    return true;
}

}  // namespace

void RemoteScreenView::showTouchSpinner(lv_point_t pt) {
    if (!touchSpinner_ || !canvasArea_) return;

    lv_area_t canvas{};
    lv_obj_get_coords(canvasArea_, &canvas);
    const int x = pt.x - canvas.x1 - 18;
    const int y = pt.y - canvas.y1 - 18;
    lv_obj_set_pos(touchSpinner_, x, y);
    lv_obj_move_foreground(touchSpinner_);
    lv_obj_clear_flag(touchSpinner_, LV_OBJ_FLAG_HIDDEN);
    touchSpinnerHideMs_ = 0;
}

void RemoteScreenView::hideTouchSpinner() {
    if (!touchSpinner_) return;
    lv_obj_add_flag(touchSpinner_, LV_OBJ_FLAG_HIDDEN);
    touchSpinnerHideMs_ = 0;
}

void RemoteScreenView::handleCanvasTouch(lv_event_t *e) {
    const lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED) return;

    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev) return;
    lv_point_t pt{};
    lv_indev_get_point(indev, &pt);
    showTouchSpinner(pt);

    if (frameW_ <= 0 || frameH_ <= 0) return;

    int u1x = 0;
    int u1y = 0;
    if (!mapRemoteTouchToU1(canvasArea_, frameW_, frameH_, pt, u1x, u1y)) return;

    const unsigned long now = millis();
    lastTouchActivityMs_ = now;

    RemoteTouchAction action = RemoteTouchAction::Move;
    if (code == LV_EVENT_PRESSED) {
        action = RemoteTouchAction::Down;
        lastSentU1X_ = u1x;
        lastSentU1Y_ = u1y;
    } else if (code == LV_EVENT_RELEASED) {
        action = RemoteTouchAction::Up;
        lastSentU1X_ = u1x;
        lastSentU1Y_ = u1y;
        touchSpinnerHideMs_ = now + 900;
    } else {
        if (now - lastTouchSendMs_ < 20) return;
        if (lastSentU1X_ >= 0 && abs(u1x - lastSentU1X_) < 4 && abs(u1y - lastSentU1Y_) < 4) return;
        lastSentU1X_ = u1x;
        lastSentU1Y_ = u1y;
    }

    lastTouchSendMs_ = now;
    app_->remoteScreen().queueTouch(u1x, u1y, action);
}

void RemoteScreenView::setLoadingVisible(bool visible, const char *text) {
    const char *msg = visible ? ((text && text[0]) ? text : "Loading…") : nullptr;
    paxx_set_loading_visible(loadingArc_, statusLbl_, visible, msg);
}

void RemoteScreenView::updateStatusLine(const char *text) {
    if (!statusLbl_) return;
    const bool show = text && text[0];
    lv_label_set_text(statusLbl_, show ? text : "");
    if (show) {
        lv_obj_clear_flag(statusLbl_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(statusLbl_, LV_OBJ_FLAG_HIDDEN);
    }
}

void RemoteScreenView::onEnter() {
    lastFetchMs_ = 0;
    lastProbeMs_ = millis();
    connectStartedMs_ = millis();
    lastTouchSendMs_ = 0;
    lastTouchActivityMs_ = 0;
    lastBlitMs_ = 0;
    lastSentU1X_ = -1;
    lastSentU1Y_ = -1;
    serviceAvailable_ = false;
    failCount_ = 0;

    if (!app_->config().remoteScreenEnabled) {
        setLoadingVisible(false);
        updateStatusLine("Enable Remote Screen in Settings");
        return;
    }

    app_->remoteScreen().setViewActive(true);

    if (!WiFi.isConnected()) {
        setLoadingVisible(true, "Waiting for WiFi…");
        return;
    }

    app_->syncServices();
    app_->remoteScreen().resetProbe();
    setLoadingVisible(true, "Connecting to U1 remote screen…");
}

void RemoteScreenView::onLeave() {
    app_->remoteScreen().setViewActive(false);
    setLoadingVisible(false);
    releaseFrame();
}

void RemoteScreenView::releaseFrame() {
    if (image_) lv_image_set_src(image_, NULL);
    frameBuf_ = nullptr;
    frameW_ = 0;
    frameH_ = 0;
    frameFormat_ = LV_COLOR_FORMAT_UNKNOWN;
    fetchInProgress_ = false;
}

void RemoteScreenView::onTick() {
    if (!app_->config().remoteScreenEnabled) return;
    if (!WiFi.isConnected()) return;

    if (touchSpinner_ && touchSpinnerHideMs_ != 0 && millis() > touchSpinnerHideMs_) {
        hideTouchSpinner();
    }

    const unsigned long sinceTouch = millis() - lastTouchActivityMs_;
#if PAXX_REMOTE_ONLY
    app_->remoteScreen().setRefreshIntervalMs(sinceTouch < 3000 ? 33UL : 100UL);
#else
    app_->remoteScreen().setRefreshIntervalMs(sinceTouch < 5000 ? 100UL : 150UL);
#endif

    const RemoteProbeState probe = app_->remoteScreen().probeState();

    if (probe == RemoteProbeState::Idle) {
        if (millis() - lastProbeMs_ > 1000) {
            lastProbeMs_ = millis();
            connectStartedMs_ = millis();
            app_->syncServices();
            app_->remoteScreen().resetProbe();
            setLoadingVisible(true, "Connecting to U1 remote screen…");
        }
        return;
    }

    uint8_t *buf = nullptr;
    lv_color_format_t format = LV_COLOR_FORMAT_UNKNOWN;
    int w = 0;
    int h = 0;
    const unsigned long now = millis();
    const bool touchActive = sinceTouch < 500;
#if PAXX_REMOTE_ONLY
    const unsigned long blitInterval = touchActive ? 16UL : 33UL;
#else
    const unsigned long blitInterval = touchActive ? 16UL : 66UL;
#endif
    if (lastBlitMs_ == 0 || now - lastBlitMs_ >= blitInterval) {
        if (app_->remoteScreen().pollFrame(buf, format, w, h) && buf) {
            const bool layoutChanged = (frameBuf_ == nullptr) || frameW_ != w || frameH_ != h;
            lastBlitMs_ = now;
            frameBuf_ = buf;
            frameFormat_ = format;
            frameW_ = w;
            frameH_ = h;
            hideTouchSpinner();
            if (layoutChanged) {
                ImageDecoder::bindLvImage(imageDsc_, image_, frameBuf_, frameFormat_, w, h,
                                          lv_obj_get_width(canvasArea_), lv_obj_get_height(canvasArea_));
            } else {
                imageDsc_.data = frameBuf_;
                lv_image_set_src(image_, &imageDsc_);
                lv_obj_invalidate(image_);
            }
            lv_obj_move_foreground(image_);
            if (touchSpinner_ && !lv_obj_has_flag(touchSpinner_, LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_move_foreground(touchSpinner_);
            }
            setLoadingVisible(false);
            lastFetchMs_ = now;
            failCount_ = 0;
            serviceAvailable_ = true;
            return;
        }
    }

    if (probe == RemoteProbeState::Failed) {
        setLoadingVisible(false);
        updateStatusLine(app_->remoteScreen().probeError());
        if (millis() - lastProbeMs_ > 8000) {
            lastProbeMs_ = millis();
            connectStartedMs_ = millis();
            setLoadingVisible(true, "Retrying remote screen…");
            app_->syncServices();
            app_->remoteScreen().resetProbe();
        }
        return;
    }

    if (probe == RemoteProbeState::Running && millis() - connectStartedMs_ > 20000) {
        app_->remoteScreen().forceProbeFailed("Remote screen probe timed out\nCheck printer IP and U1 Remote Screen");
        setLoadingVisible(false);
        updateStatusLine(app_->remoteScreen().probeError());
        return;
    }

    if (probe == RemoteProbeState::Ok && lastFetchMs_ == 0 && millis() - connectStartedMs_ > 12000) {
        const char *snapErr = app_->remoteScreen().lastSnapshotError();
        setLoadingVisible(false);
        if (snapErr && snapErr[0]) {
            updateStatusLine(snapErr);
        } else {
            updateStatusLine("Snapshot failed — enable Remote Screen on U1 and reboot");
        }
        if (millis() - lastProbeMs_ > 8000) {
            lastProbeMs_ = millis();
            connectStartedMs_ = millis();
            setLoadingVisible(true, "Retrying remote screen…");
            app_->remoteScreen().resetProbe();
        }
        return;
    }

    if (lastFetchMs_ == 0) {
        setLoadingVisible(true, "Connecting to U1 remote screen…");
    } else if (!frameBuf_ && millis() - lastFetchMs_ > 6000) {
        setLoadingVisible(true, "Waiting for remote screen frames…");
    } else if (frameBuf_) {
        setLoadingVisible(false);
    }
}

void TimelapseScreen::create(PaxxApp *app, lv_obj_t *parent) {
    app_ = app;
    screen_ = lv_obj_create(parent);
    lv_obj_set_size(screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(screen_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_, 0, LV_PART_MAIN);
    paxx_create_nav_bar(screen_, "Timelapses", paxx_back_home_cb, app, app->isDark());

    list_ = lv_list_create(screen_);
    lv_obj_align(list_, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_size(list_, LV_PCT(96), 300);

    detailLbl_ = lv_label_create(screen_);
    lv_obj_align(detailLbl_, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_width(detailLbl_, LV_PCT(95));
    lv_label_set_long_mode(detailLbl_, LV_LABEL_LONG_WRAP);
}

void TimelapseScreen::onEnter() { refreshList(); }

void TimelapseScreen::refreshList() {
    if (!list_) return;
    lv_obj_clean(list_);
    timelapseCtxs_.clear();
    lv_label_set_text(detailLbl_, "Loading...");

    app_->moonrakerRest().listTimelapses([this](bool ok, const std::vector<TimelapseEntry> &items) {
        lv_obj_clean(list_);
        timelapseCtxs_.clear();
        if (!ok || items.empty()) {
            lv_label_set_text(detailLbl_, "No timelapses found.\nEnsure paxx12 camera + timelapse enabled.");
            lv_list_add_text(list_, "No .mp4 files in Moonraker timelapse/gcodes roots");
            return;
        }
        lv_label_set_text_fmt(detailLbl_, "%d timelapse(s) on printer", static_cast<int>(items.size()));
        timelapseCtxs_.reserve(items.size());
        for (const TimelapseEntry &t : items) {
            timelapseCtxs_.push_back({app_, t});
            const TimelapseCtx &ctx = timelapseCtxs_.back();
            char line[128];
            snprintf(line, sizeof(line), "%s (%.1f MB)", t.name, t.size / (1024.0f * 1024.0f));
            lv_obj_t *btn = lv_list_add_button(list_, LV_SYMBOL_VIDEO, line);
            lv_obj_add_event_cb(btn, [](lv_event_t *e) {
                auto *ctx = static_cast<TimelapseCtx *>(lv_event_get_user_data(e));
                char msg[160];
                snprintf(msg, sizeof(msg), "Timelapse: %s\nPath: %s\nOpen Fluidd/Mainsail to play MP4.",
                         ctx->entry.name, ctx->entry.path);
                PaxxNotify::show("Timelapse", msg, 6000);
            }, LV_EVENT_CLICKED, const_cast<TimelapseCtx *>(&ctx));
        }
    });
}

void CameraScreen::create(PaxxApp *app, lv_obj_t *parent) {
    app_ = app;
    screen_ = lv_obj_create(parent);
    lv_obj_set_size(screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(screen_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_, 0, LV_PART_MAIN);
    paxx_create_nav_bar(screen_, "Camera", paxx_back_home_cb, app, app->isDark());

    statusLbl_ = lv_label_create(screen_);
    lv_obj_align(statusLbl_, LV_ALIGN_TOP_MID, 0, 52);
    lv_label_set_text(statusLbl_, "Live snapshot from /webcam/");

    lv_obj_t *frame = lv_obj_create(screen_);
    lv_obj_align(frame, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_size(frame, 640, 360);
    lv_obj_set_style_bg_color(frame, lv_color_black(), LV_PART_MAIN);
    image_ = lv_image_create(frame);
    lv_obj_center(image_);
}

void CameraScreen::onEnter() {
    lastFetchMs_ = 0;
    lastRequestMs_ = 0;
    app_->syncServices();
    app_->camera().requestFetch();
}

void CameraScreen::onLeave() {
    releaseFrame();
}

void CameraScreen::releaseFrame() {
    if (image_) lv_image_set_src(image_, NULL);
    if (frameBuf_) {
        ImageDecoder::freeBuffer(frameBuf_);
        frameBuf_ = nullptr;
    }
}

void CameraScreen::onTick() {
    uint16_t *buf = nullptr;
    int w = 0;
    int h = 0;
    if (app_->camera().poll(buf, w, h) && buf) {
        if (image_) lv_image_set_src(image_, NULL);
        if (frameBuf_) ImageDecoder::freeBuffer(frameBuf_);
        frameBuf_ = buf;
        ImageDecoder::bindLvImage(imageDsc_, image_, buf, LV_COLOR_FORMAT_RGB565, w, h);
        lv_label_set_text_fmt(statusLbl_, "Camera %dx%d", w, h);
        lastFetchMs_ = millis();
    } else if (lastFetchMs_ == 0 && millis() - lastRequestMs_ > 12000) {
        lv_label_set_text_fmt(statusLbl_,
            "Camera unavailable (HTTP %d)\nCheck printer webcam / API key",
            app_->camera().lastHttpCode());
    }

    if (millis() - lastRequestMs_ < 1500) return;
    lastRequestMs_ = millis();
    app_->camera().requestFetch();
}

void FilesScreen::create(PaxxApp *app, lv_obj_t *parent) {
    app_ = app;
    screen_ = lv_obj_create(parent);
    lv_obj_set_size(screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(screen_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_, 0, LV_PART_MAIN);
    paxx_create_nav_bar(screen_, "Print Files", paxx_back_home_cb, app, app->isDark());

    list_ = lv_list_create(screen_);
    lv_obj_align(list_, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_size(list_, LV_PCT(96), 340);

    statusLbl_ = lv_label_create(screen_);
    lv_obj_align(statusLbl_, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_label_set_text(statusLbl_, "Tap a file to map colors and print");
}

void FilesScreen::onEnter() { refreshList(); }

void FilesScreen::refreshList() {
    if (!list_) return;
    lv_obj_clean(list_);
    fileCtxs_.clear();
    lv_label_set_text(statusLbl_, "Loading files…");
    app_->ensureMoonrakerRest();

    app_->moonrakerRest().listFiles("gcodes", "", [this](bool ok, const std::vector<MoonrakerFileEntry> &files) {
        lv_obj_clean(list_);
        fileCtxs_.clear();
        if (!ok) {
            lv_label_set_text_fmt(statusLbl_, "Failed to list files (HTTP %d)",
                                  app_->moonrakerRest().lastStatusCode());
            lv_list_add_text(list_, "Check Moonraker on port 7125");
            return;
        }
        if (files.empty()) {
            lv_label_set_text(statusLbl_, "No files in gcodes root");
            lv_list_add_text(list_, "Upload G-code via Fluidd/Mainsail");
            return;
        }
        int count = 0;
        fileCtxs_.reserve(48);
        for (const auto &f : files) {
            if (f.isDir) continue;
            const char *name = strrchr(f.path, '/');
            name = name ? name + 1 : f.path;
            if (!strstr(name, ".gcode") && !strstr(name, ".gco") && !strstr(name, ".GCODE")) continue;

            fileCtxs_.push_back({});
            FileCtx &ctx = fileCtxs_.back();
            ctx.app = app_;
            strlcpy(ctx.path, f.path, sizeof(ctx.path));

            const size_t idx = fileCtxs_.size() - 1;
            lv_obj_t *btn = lv_list_add_button(list_, LV_SYMBOL_FILE, name);
            lv_obj_set_user_data(btn, reinterpret_cast<void *>(idx));
            lv_obj_add_event_cb(btn, [](lv_event_t *e) {
                auto *self = static_cast<FilesScreen *>(lv_event_get_user_data(e));
                const size_t idx = reinterpret_cast<size_t>(
                    lv_obj_get_user_data(static_cast<lv_obj_t *>(lv_event_get_target(e))));
                if (!self || idx >= self->fileCtxs_.size()) return;
                self->app_->showPrintPrepare(self->fileCtxs_[idx].path);
            }, LV_EVENT_CLICKED, this);
            if (++count >= 40) break;
        }
        lv_label_set_text_fmt(statusLbl_, "%d printable file(s) — tap to prepare", count);
    });
}

void PrintPrepareScreen::create(PaxxApp *app, lv_obj_t *parent) {
    app_ = app;
    screen_ = lv_obj_create(parent);
    lv_obj_set_size(screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(screen_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_, 0, LV_PART_MAIN);
    paxx_create_nav_bar(screen_, "Prepare Print", paxx_back_files_cb, app, app->isDark());

    titleLbl_ = lv_label_create(screen_);
    lv_obj_align(titleLbl_, LV_ALIGN_TOP_LEFT, 8, 56);
    lv_obj_set_width(titleLbl_, LV_PCT(96));
    lv_label_set_long_mode(titleLbl_, LV_LABEL_LONG_DOT);

    metaLbl_ = lv_label_create(screen_);
    lv_obj_align(metaLbl_, LV_ALIGN_TOP_LEFT, 8, 78);
    lv_obj_set_width(metaLbl_, LV_PCT(96));
    lv_label_set_long_mode(metaLbl_, LV_LABEL_LONG_WRAP);

    rowsPanel_ = lv_obj_create(screen_);
    lv_obj_align(rowsPanel_, LV_ALIGN_TOP_MID, 0, 108);
    lv_obj_set_size(rowsPanel_, LV_PCT(96), 240);
    lv_obj_set_style_bg_opa(rowsPanel_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(rowsPanel_, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(rowsPanel_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(rowsPanel_, 8, LV_PART_MAIN);

    hintLbl_ = lv_label_create(screen_);
    lv_obj_align(hintLbl_, LV_ALIGN_BOTTOM_MID, 0, -52);
    lv_obj_set_width(hintLbl_, LV_PCT(96));
    lv_label_set_text(hintLbl_, "Map each print color to a toolhead, then confirm.");

    lv_obj_t *cancelBtn = lv_btn_create(screen_);
    lv_obj_align(cancelBtn, LV_ALIGN_BOTTOM_LEFT, 8, -8);
    lv_obj_add_event_cb(cancelBtn, [](lv_event_t *e) {
        static_cast<PaxxApp *>(lv_event_get_user_data(e))->showFiles();
    }, LV_EVENT_CLICKED, app);
    lv_label_set_text(lv_label_create(cancelBtn), "Cancel");

    lv_obj_t *startBtn = lv_btn_create(screen_);
    lv_obj_align(startBtn, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    lv_obj_set_style_bg_color(startBtn, PaxxTheme::accent(), LV_PART_MAIN);
    lv_obj_add_event_cb(startBtn, [](lv_event_t *e) {
        static_cast<PrintPrepareScreen *>(lv_event_get_user_data(e))->startPrintJob();
    }, LV_EVENT_CLICKED, this);
    lv_label_set_text(lv_label_create(startBtn), "Start Print");
}

void PrintPrepareScreen::open(const char *gcodePath) {
    gcodePath_[0] = '\0';
    meta_ = {};
    if (!gcodePath || !gcodePath[0]) return;

    strlcpy(gcodePath_, gcodePath, sizeof(gcodePath_));
    const char *name = strrchr(gcodePath_, '/');
    name = name ? name + 1 : gcodePath_;
    lv_label_set_text_fmt(titleLbl_, "File: %s", name);
    lv_label_set_text(metaLbl_, "Loading metadata…");

    app_->ensureMoonrakerRest();
    if (!app_->moonrakerRest().getGcodeMetadata(gcodePath_, meta_)) {
        lv_label_set_text(metaLbl_, "Could not read file metadata — using defaults");
        meta_.colorCount = 1;
        strlcpy(meta_.colors[0].hex, "#888888", sizeof(meta_.colors[0].hex));
    } else {
        lv_label_set_text_fmt(metaLbl_, "~%.0f min · %d color(s) — assign each to T0–T3",
                              meta_.estimatedMinutes, meta_.colorCount);
    }

    autoMapColors();
    rebuildRows();
}

void PrintPrepareScreen::autoMapColors() {
    const PrinterStatus &st = app_->moonraker().status();
    for (int i = 0; i < meta_.colorCount && i < 4; ++i) {
        toolMap_[i] = bestToolForPrintColor(meta_.colors[i].hex, st, i);
    }
}

void PrintPrepareScreen::setToolForColor(int colorIndex, int tool) {
    if (colorIndex < 0 || colorIndex >= meta_.colorCount || colorIndex >= 4) return;
    toolMap_[colorIndex] = constrain(tool, 0, 3);
    rebuildRows();
}

void PrintPrepareScreen::onToolPick(lv_event_t *e) {
    auto *self = static_cast<PrintPrepareScreen *>(lv_event_get_user_data(e));
    const intptr_t packed = reinterpret_cast<intptr_t>(
        lv_obj_get_user_data(static_cast<lv_obj_t *>(lv_event_get_target(e))));
    self->setToolForColor(static_cast<int>(packed / 10), static_cast<int>(packed % 10));
}

void PrintPrepareScreen::rebuildRows() {
    if (!rowsPanel_) return;
    lv_obj_clean(rowsPanel_);
    memset(rows_, 0, sizeof(rows_));

    const PrinterStatus &st = app_->moonraker().status();
    const bool dark = app_->isDark();

    for (int i = 0; i < meta_.colorCount && i < 4; ++i) {
        lv_obj_t *row = lv_obj_create(rowsPanel_);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 52);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 8, LV_PART_MAIN);
        paxx_disable_input(row);

        lv_obj_t *swatch = lv_obj_create(row);
        lv_obj_set_size(swatch, 36, 36);
        lv_obj_set_style_bg_color(swatch, hexToLvColor(meta_.colors[i].hex), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(swatch, 6, LV_PART_MAIN);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text_fmt(lbl, "Color %d\n%s%.1fg",
                              i + 1, meta_.colors[i].hex,
                              meta_.colors[i].weightG);

        for (int t = 0; t < 4; ++t) {
            lv_obj_t *btn = lv_btn_create(row);
            lv_obj_set_size(btn, 44, 36);
            lv_label_set_text_fmt(lv_label_create(btn), "T%d", t);
            lv_obj_set_user_data(btn, reinterpret_cast<void *>(static_cast<intptr_t>(i * 10 + t)));
            lv_obj_add_event_cb(btn, onToolPick, LV_EVENT_CLICKED, this);

            const bool selected = toolMap_[i] == t;
            lv_obj_set_style_bg_color(btn,
                selected ? PaxxTheme::primary() : PaxxTheme::surface(dark), LV_PART_MAIN);

            char loadedHint[24] = {};
            const char *slotColor = "#888888";
            for (const FilamentSlot &f : st.filaments) {
                if (f.index == t) {
                    snprintf(loadedHint, sizeof(loadedHint), "%s", f.material);
                    slotColor = f.color;
                    break;
                }
            }
            if (loadedHint[0]) {
                lv_obj_set_style_border_color(btn, hexToLvColor(slotColor), LV_PART_MAIN);
                lv_obj_set_style_border_width(btn, selected ? 3 : 1, LV_PART_MAIN);
            }
            rows_[i].toolBtns[t] = btn;
        }
    }
}

void PrintPrepareScreen::startPrintJob() {
    if (gcodePath_[0] == '\0') return;

    app_->ensureMoonrakerRest();
    lv_label_set_text(hintLbl_, "Applying tool map and starting print…");

    if (!app_->moonrakerRest().setExtruderMapTable(toolMap_, meta_.colorCount)) {
        lv_label_set_text(hintLbl_, "Tool map failed — check Moonraker connection");
        PaxxNotify::show("Print", "Failed to set color mapping");
        return;
    }

    if (!app_->moonrakerRest().startPrint("gcodes", gcodePath_)) {
        lv_label_set_text(hintLbl_, "Print start failed");
        PaxxNotify::show("Print", "Failed to start print");
        return;
    }

    PaxxNotify::show("Print", "Print started");
    app_->showPrint();
}

void ControlsScreen::create(PaxxApp *app, lv_obj_t *parent) {
    app_ = app;
    screen_ = lv_obj_create(parent);
    lv_obj_set_size(screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(screen_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_, 0, LV_PART_MAIN);
    paxx_create_nav_bar(screen_, "Controls", paxx_back_home_cb, app, app->isDark());

    lv_obj_t *scroll = lv_obj_create(screen_);
    lv_obj_align(scroll, LV_ALIGN_TOP_LEFT, 0, kNavBarHeight);
    lv_obj_set_size(scroll, LV_PCT(100), PT_LCD_V_RES - kNavBarHeight);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(scroll, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scroll, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(scroll, 8, LV_PART_MAIN);
    lv_obj_add_flag(scroll, LV_OBJ_FLAG_SCROLLABLE);

    statusLbl_ = lv_label_create(scroll);
    lv_label_set_text(statusLbl_, "Jog, fans, and macros (REST fallback when WS offline)");

    auto addRow = [&](lv_obj_t *parentRow) {
        lv_obj_t *row = lv_obj_create(scroll);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, 8, LV_PART_MAIN);
        paxx_disable_input(row);
        return row;
    };

    auto addBtn = [&](lv_obj_t *row, const char *label, lv_event_cb_t cb) {
        lv_obj_t *btn = lv_btn_create(row);
        lv_obj_set_height(btn, 40);
        lv_label_set_text(lv_label_create(btn), label);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, app);
        return btn;
    };

    lv_obj_t *printRow = addRow(scroll);
    addBtn(printRow, "Pause", [](lv_event_t *e) { static_cast<PaxxApp *>(lv_event_get_user_data(e))->sendGcode("PAUSE"); });
    addBtn(printRow, "Resume", [](lv_event_t *e) { static_cast<PaxxApp *>(lv_event_get_user_data(e))->sendGcode("RESUME"); });
    lv_obj_t *cancelBtn = addBtn(printRow, "Cancel", [](lv_event_t *e) { static_cast<PaxxApp *>(lv_event_get_user_data(e))->sendGcode("CANCEL_PRINT"); });
    lv_obj_set_style_bg_color(cancelBtn, PaxxTheme::danger(), LV_PART_MAIN);

    lv_obj_t *homeRow = addRow(scroll);
    addBtn(homeRow, "Home All", [](lv_event_t *e) { static_cast<PaxxApp *>(lv_event_get_user_data(e))->sendGcode("G28"); });
    addBtn(homeRow, "Bed Mesh", [](lv_event_t *e) { static_cast<PaxxApp *>(lv_event_get_user_data(e))->sendGcode("BED_MESH_CALIBRATE"); });

    lv_obj_t *jogRow = addRow(scroll);
    addBtn(jogRow, "X-", [](lv_event_t *e) { static_cast<PaxxApp *>(lv_event_get_user_data(e))->sendGcode("G91\nG1 X-10 F6000\nG90"); });
    addBtn(jogRow, "X+", [](lv_event_t *e) { static_cast<PaxxApp *>(lv_event_get_user_data(e))->sendGcode("G91\nG1 X10 F6000\nG90"); });
    addBtn(jogRow, "Y-", [](lv_event_t *e) { static_cast<PaxxApp *>(lv_event_get_user_data(e))->sendGcode("G91\nG1 Y-10 F6000\nG90"); });
    addBtn(jogRow, "Y+", [](lv_event_t *e) { static_cast<PaxxApp *>(lv_event_get_user_data(e))->sendGcode("G91\nG1 Y10 F6000\nG90"); });

    lv_obj_t *zRow = addRow(scroll);
    addBtn(zRow, "Z-", [](lv_event_t *e) { static_cast<PaxxApp *>(lv_event_get_user_data(e))->sendGcode("G91\nG1 Z-1 F1200\nG90"); });
    addBtn(zRow, "Z+", [](lv_event_t *e) { static_cast<PaxxApp *>(lv_event_get_user_data(e))->sendGcode("G91\nG1 Z1 F1200\nG90"); });

    lv_obj_t *fanLbl = lv_label_create(scroll);
    lv_label_set_text(fanLbl, "Part fan %");
    fanSlider_ = lv_slider_create(scroll);
    lv_obj_set_width(fanSlider_, LV_PCT(95));
    lv_slider_set_range(fanSlider_, 0, 100);
    lv_obj_add_event_cb(fanSlider_, [](lv_event_t *e) {
        if (lv_event_get_code(e) != LV_EVENT_RELEASED) return;
        auto *a = static_cast<PaxxApp *>(lv_event_get_user_data(e));
        char cmd[16];
        snprintf(cmd, sizeof(cmd), "M106 S%d", lv_slider_get_value(static_cast<lv_obj_t *>(lv_event_get_target(e))) * 255 / 100);
        a->sendGcode(cmd);
    }, LV_EVENT_ALL, app);

    lv_obj_t *speedLbl = lv_label_create(scroll);
    lv_label_set_text(speedLbl, "Speed %");
    speedSlider_ = lv_slider_create(scroll);
    lv_obj_set_width(speedSlider_, LV_PCT(95));
    lv_slider_set_range(speedSlider_, 10, 200);
    lv_obj_add_event_cb(speedSlider_, [](lv_event_t *e) {
        if (lv_event_get_code(e) != LV_EVENT_RELEASED) return;
        auto *a = static_cast<PaxxApp *>(lv_event_get_user_data(e));
        char cmd[16];
        snprintf(cmd, sizeof(cmd), "M220 S%d", lv_slider_get_value(static_cast<lv_obj_t *>(lv_event_get_target(e))));
        a->sendGcode(cmd);
    }, LV_EVENT_ALL, app);

    lv_obj_t *flowLbl = lv_label_create(scroll);
    lv_label_set_text(flowLbl, "Flow %");
    flowSlider_ = lv_slider_create(scroll);
    lv_obj_set_width(flowSlider_, LV_PCT(95));
    lv_slider_set_range(flowSlider_, 10, 200);
    lv_obj_add_event_cb(flowSlider_, [](lv_event_t *e) {
        if (lv_event_get_code(e) != LV_EVENT_RELEASED) return;
        auto *a = static_cast<PaxxApp *>(lv_event_get_user_data(e));
        char cmd[16];
        snprintf(cmd, sizeof(cmd), "M221 S%d", lv_slider_get_value(static_cast<lv_obj_t *>(lv_event_get_target(e))));
        a->sendGcode(cmd);
    }, LV_EVENT_ALL, app);
}

void ControlsScreen::update(const PrinterStatus &status) {
    const int speed = static_cast<int>(status.speedFactor);
    const int flow = static_cast<int>(status.flowFactor);
    if (speedSlider_ && speed != lastSpeed_) {
        lastSpeed_ = speed;
        lv_slider_set_value(speedSlider_, speed, LV_ANIM_OFF);
    }
    if (flowSlider_ && flow != lastFlow_) {
        lastFlow_ = flow;
        lv_slider_set_value(flowSlider_, flow, LV_ANIM_OFF);
    }
    if (statusLbl_) {
        lv_label_set_text_fmt(statusLbl_, "State: %s  Tool T%d",
                              printStateLabel(status.printState), status.activeTool);
    }
}

void TerminalScreen::create(PaxxApp *app, lv_obj_t *parent) {
    app_ = app;
    screen_ = lv_obj_create(parent);
    lv_obj_set_size(screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(screen_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_, 0, LV_PART_MAIN);
    paxx_create_nav_bar(screen_, "Terminal", paxx_back_home_cb, app, app->isDark());

    logTa_ = lv_textarea_create(screen_);
    lv_obj_align(logTa_, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_size(logTa_, LV_PCT(96), 300);
    lv_textarea_set_one_line(logTa_, false);
    lv_obj_add_state(logTa_, LV_STATE_DISABLED);
    lv_textarea_set_placeholder_text(logTa_, "Console output appears here…");

    cmdTa_ = lv_textarea_create(screen_);
    lv_obj_set_width(cmdTa_, LV_PCT(70));
    lv_obj_align(cmdTa_, LV_ALIGN_BOTTOM_LEFT, 8, -8);
    lv_textarea_set_one_line(cmdTa_, true);
    lv_textarea_set_placeholder_text(cmdTa_, "G-code command");
    PaxxKeyboard::attach(cmdTa_);

    lv_obj_t *sendBtn = lv_btn_create(screen_);
    lv_obj_align(sendBtn, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    lv_obj_add_event_cb(sendBtn, [](lv_event_t *e) {
        auto *a = static_cast<PaxxApp *>(lv_event_get_user_data(e));
        const char *cmd = lv_textarea_get_text(a->terminal().cmdInput());
        if (!cmd || !cmd[0]) return;
        if (a->sendGcode(cmd)) {
            lv_textarea_set_text(a->terminal().cmdInput(), "");
            PaxxKeyboard::hide();
        } else {
            paxx_log("Send failed: %s", cmd);
        }
    }, LV_EVENT_CLICKED, app);
    lv_label_set_text(lv_label_create(sendBtn), "Send");
}

void TerminalScreen::onEnter() {
    lastRefreshMs_ = 0;
    onTick();
}

void TerminalScreen::onTick() {
    if (!logTa_ || millis() - lastRefreshMs_ < 500) return;
    lastRefreshMs_ = millis();
    char buf[4096];
    ConsoleLog::instance().getText(buf, sizeof(buf));
    lv_textarea_set_text(logTa_, buf);
    lv_textarea_set_cursor_pos(logTa_, LV_TEXTAREA_CURSOR_LAST);
}

void SettingsScreen::setHint(const char *text) {
    if (hintLbl_) lv_label_set_text(hintLbl_, text ? text : "");
}

void SettingsScreen::create(PaxxApp *app, lv_obj_t *parent) {
    app_ = app;
    screen_ = lv_obj_create(parent);
    lv_obj_set_size(screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(screen_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_, 0, LV_PART_MAIN);
    paxx_create_nav_bar(screen_, "Settings", paxx_back_home_cb, app, app->isDark());

    hintLbl_ = lv_label_create(screen_);
    lv_obj_align(hintLbl_, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_width(hintLbl_, LV_PCT(95));
    lv_label_set_long_mode(hintLbl_, LV_LABEL_LONG_WRAP);

    lv_obj_t *list = lv_list_create(screen_);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_set_size(list, LV_PCT(96), 280);

    auto add = [&](const char *icon, const char *label, lv_event_cb_t cb) {
        lv_obj_t *b = lv_list_add_button(list, icon, label);
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, app);
    };

#if PAXX_REMOTE_ONLY
    add(LV_SYMBOL_WIFI, "WiFi Setup", [](lv_event_t *e) { static_cast<PaxxApp *>(lv_event_get_user_data(e))->showWifi(); });
    add(LV_SYMBOL_WIFI, "Printer Connection", [](lv_event_t *e) { static_cast<PaxxApp *>(lv_event_get_user_data(e))->showSetup(); });
    add(LV_SYMBOL_IMAGE, "Remote Screen URL", [](lv_event_t *e) {
        auto *a = static_cast<PaxxApp *>(lv_event_get_user_data(e));
        const PrinterProfile &p = activeProfile(a->config());
        char buf[96];
        paxxFormatScreenUrl(p.host, buf, sizeof(buf));
        a->settings().setHint(buf);
    });
    add(LV_SYMBOL_TINT, "Toggle Dark Theme", [](lv_event_t *e) {
        auto *a = static_cast<PaxxApp *>(lv_event_get_user_data(e));
        a->config().darkTheme = !a->config().darkTheme;
        a->saveConfig();
        PaxxTheme::apply(a->config().darkTheme);
        a->settings().setHint("Theme updated — reopen screens to refresh");
    });
    add(LV_SYMBOL_LIST, "About", [](lv_event_t *e) {
        auto *a = static_cast<PaxxApp *>(lv_event_get_user_data(e));
        char buf[128];
        snprintf(buf, sizeof(buf), "PaxxTouch Remote v" PAXXTOUCH_VERSION " — U1 mirror at http://<ip>/screen/");
        a->settings().setHint(buf);
    });

    lv_obj_t *about = lv_label_create(screen_);
    lv_obj_align(about, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_label_set_text(about, "PaxxTouch Remote v" PAXXTOUCH_VERSION);
#else
    add(LV_SYMBOL_WIFI, "WiFi Setup", [](lv_event_t *e) { static_cast<PaxxApp *>(lv_event_get_user_data(e))->showWifi(); });
    add(LV_SYMBOL_WIFI, "Printer Connection", [](lv_event_t *e) { static_cast<PaxxApp *>(lv_event_get_user_data(e))->showSetup(); });
    add(LV_SYMBOL_SETTINGS, "Firmware Config URL", [](lv_event_t *e) {
        auto *a = static_cast<PaxxApp *>(lv_event_get_user_data(e));
        const PrinterProfile &p = activeProfile(a->config());
        char buf[96];
        snprintf(buf, sizeof(buf), "http://%s/firmware-config/", p.host);
        a->settings().setHint(buf);
    });
    add(LV_SYMBOL_IMAGE, "Toggle Remote Screen", [](lv_event_t *e) {
        auto *a = static_cast<PaxxApp *>(lv_event_get_user_data(e));
        a->config().remoteScreenEnabled = !a->config().remoteScreenEnabled;
        a->saveConfig();
        a->syncServices();
        a->settings().setHint(a->config().remoteScreenEnabled ? "Remote Screen ON" : "Remote Screen OFF");
    });
    add(LV_SYMBOL_TINT, "Toggle Dark Theme", [](lv_event_t *e) {
        auto *a = static_cast<PaxxApp *>(lv_event_get_user_data(e));
        a->config().darkTheme = !a->config().darkTheme;
        a->saveConfig();
        PaxxTheme::apply(a->config().darkTheme);
        a->settings().setHint("Theme updated — reopen screens to refresh");
    });
    add(LV_SYMBOL_BELL, "Toggle Notifications", [](lv_event_t *e) {
        auto *a = static_cast<PaxxApp *>(lv_event_get_user_data(e));
        a->config().notificationsEnabled = !a->config().notificationsEnabled;
        a->saveConfig();
        a->settings().setHint(a->config().notificationsEnabled ? "Notifications ON" : "Notifications OFF");
    });
    add(LV_SYMBOL_REFRESH, "Switch Profile (cycle)", [](lv_event_t *e) {
        auto *a = static_cast<PaxxApp *>(lv_event_get_user_data(e));
        if (a->config().profileCount <= 0) return;
        a->config().activeProfile = (a->config().activeProfile + 1) % a->config().profileCount;
        a->saveConfig();
        a->applyProfile();
        char buf[64];
        snprintf(buf, sizeof(buf), "Active: %s", activeProfile(a->config()).name);
        a->settings().setHint(buf);
    });
    add(LV_SYMBOL_DOWNLOAD, "OTA: ready when on WiFi", [](lv_event_t *e) {
        static_cast<PaxxApp *>(lv_event_get_user_data(e))->settings().setHint("Flash OTA via Arduino IDE or pio upload --upload-port IP");
    });
    add(LV_SYMBOL_LIST, "About", [](lv_event_t *e) {
        auto *a = static_cast<PaxxApp *>(lv_event_get_user_data(e));
        char buf[128];
        snprintf(buf, sizeof(buf), "PaxxTouch v" PAXXTOUCH_VERSION " — Snapmaker U1 + Paxx Extended Firmware");
        a->settings().setHint(buf);
    });

    lv_obj_t *about = lv_label_create(screen_);
    lv_obj_align(about, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_label_set_text(about, "PaxxTouch v" PAXXTOUCH_VERSION);
#endif
}

void SetupScreen::updateNavBack() {
#if PAXX_REMOTE_ONLY
    if (!navBackBtn_) return;
    if (PaxxPreferences::instance().hasPrinter()) {
        lv_obj_clear_flag(navBackBtn_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(navBackBtn_, LV_OBJ_FLAG_HIDDEN);
    }
#endif
}

void SetupScreen::toggleAdvanced() {
    if (!advancedPanel_ || !advancedBtn_) return;
    advancedVisible_ = !advancedVisible_;
    if (advancedVisible_) {
        lv_obj_clear_flag(advancedPanel_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(lv_obj_get_child(advancedBtn_, 0), LV_SYMBOL_DOWN " Hide advanced");
    } else {
        lv_obj_add_flag(advancedPanel_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(lv_obj_get_child(advancedBtn_, 0), LV_SYMBOL_SETTINGS " Advanced options");
    }
}

void SetupScreen::onEnter() {
    updateNavBack();
#if PAXX_REMOTE_ONLY
    if (advancedPanel_) {
        advancedVisible_ = false;
        lv_obj_add_flag(advancedPanel_, LV_OBJ_FLAG_HIDDEN);
        if (advancedBtn_) {
            lv_label_set_text(lv_obj_get_child(advancedBtn_, 0), LV_SYMBOL_SETTINGS " Advanced options");
        }
    }
    if (hostTa_) {
        PaxxKeyboard::promptFor(hostTa_);
    }
    if (hintLbl_) {
        char urlHint[96];
        const char *host = app_ ? lv_textarea_get_text(hostTa_) : "";
        paxxFormatScreenUrl(host && host[0] ? host : nullptr, urlHint, sizeof(urlHint));
        lv_label_set_text(hintLbl_, urlHint);
    }
#endif
}

void SetupScreen::create(PaxxApp *app, lv_obj_t *parent) {
    app_ = app;
    screen_ = lv_obj_create(parent);
    lv_obj_set_size(screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(screen_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_, 0, LV_PART_MAIN);
    paxx_style_form_screen(screen_);

#if PAXX_REMOTE_ONLY
    paxx_create_nav_bar(screen_, "Printer Setup", paxx_back_remote_cb, app, app->isDark(), &navBackBtn_);

    const PrinterProfile &p = activeProfile(app->config());
    int y = 56;

    hostTa_ = lv_textarea_create(screen_);
    paxx_set_form_width(hostTa_);
    lv_obj_align(hostTa_, LV_ALIGN_TOP_MID, 0, y);
    y += 52;
    lv_textarea_set_one_line(hostTa_, true);
    lv_textarea_set_placeholder_text(hostTa_, "Printer IP address (e.g. 192.168.1.100)");
    if (p.host[0]) lv_textarea_set_text(hostTa_, p.host);
    PaxxKeyboard::attach(hostTa_, PaxxKbMode::Number);

    hintLbl_ = lv_label_create(screen_);
    paxx_set_form_width(hintLbl_);
    lv_obj_align(hintLbl_, LV_ALIGN_TOP_MID, 0, y);
    y += 28;
    lv_label_set_long_mode(hintLbl_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(hintLbl_, PaxxTheme::muted(app->isDark()), LV_PART_MAIN);
    {
        char urlHint[96];
        paxxFormatScreenUrl(p.host[0] ? p.host : nullptr, urlHint, sizeof(urlHint));
        lv_label_set_text(hintLbl_, urlHint);
    }

    advancedBtn_ = lv_btn_create(screen_);
    paxx_set_form_width(advancedBtn_);
    lv_obj_set_height(advancedBtn_, 36);
    lv_obj_align(advancedBtn_, LV_ALIGN_TOP_MID, 0, y);
    y += 44;
    lv_obj_add_event_cb(advancedBtn_, [](lv_event_t *e) {
        static_cast<SetupScreen *>(lv_event_get_user_data(e))->toggleAdvanced();
    }, LV_EVENT_CLICKED, this);
    lv_label_set_text(lv_label_create(advancedBtn_), LV_SYMBOL_SETTINGS " Advanced options");

    advancedPanel_ = lv_obj_create(screen_);
    paxx_set_form_width(advancedPanel_);
    lv_obj_align(advancedPanel_, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_opa(advancedPanel_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(advancedPanel_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(advancedPanel_, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(advancedPanel_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(advancedPanel_, 8, LV_PART_MAIN);
    lv_obj_add_flag(advancedPanel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(advancedPanel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(advancedPanel_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(advancedPanel_, LV_DIR_VER);
    lv_obj_set_height(advancedPanel_, 160);

    auto addAdvField = [&](const char *ph, lv_obj_t **ta, const char *val, PaxxKbMode mode = PaxxKbMode::Text) {
        *ta = lv_textarea_create(advancedPanel_);
        paxx_set_form_width(*ta);
        lv_textarea_set_one_line(*ta, true);
        lv_textarea_set_placeholder_text(*ta, ph);
        if (val && val[0]) lv_textarea_set_text(*ta, val);
        PaxxKeyboard::attach(*ta, mode);
    };

    addAdvField("Moonraker port (7125)", &portTa_, String(p.moonrakerPort ? p.moonrakerPort : 7125).c_str(), PaxxKbMode::Number);
    addAdvField("API key (optional)", &keyTa_, p.apiKey);
    addAdvField("Username (optional)", &userTa_, p.username);
    addAdvField("Password (optional)", &passTa_, p.password, PaxxKbMode::Password);
    nameTa_ = nullptr;

    lv_obj_t *save = lv_btn_create(screen_);
    lv_obj_align(save, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_add_event_cb(save, [](lv_event_t *e) {
        auto *a = static_cast<PaxxApp *>(lv_event_get_user_data(e));
        PrinterProfile &prof = activeProfile(a->config());
        const char *host = lv_textarea_get_text(a->setup().hostInput());
        if (!host || !host[0]) {
            PaxxNotify::show("Printer", "Enter printer IP address");
            return;
        }
        strlcpy(prof.name, "Printer", sizeof(prof.name));
        strlcpy(prof.host, host, sizeof(prof.host));
        prof.moonrakerPort = static_cast<uint16_t>(atoi(lv_textarea_get_text(a->setup().portInput())));
        if (prof.moonrakerPort == 0) prof.moonrakerPort = 7125;
        strlcpy(prof.apiKey, lv_textarea_get_text(a->setup().keyInput()), sizeof(prof.apiKey));
        strlcpy(prof.username, lv_textarea_get_text(a->setup().userInput()), sizeof(prof.username));
        strlcpy(prof.password, lv_textarea_get_text(a->setup().passInput()), sizeof(prof.password));
        prof.useAuth = prof.username[0] != '\0' || prof.apiKey[0] != '\0';
        if (a->config().profileCount <= 0) a->config().profileCount = 1;
        a->saveConfig();
        a->showGlobalLoading(true, "Connecting to printer…");
        a->applyProfile();
        a->showRemote();
        a->showGlobalLoading(false);
    }, LV_EVENT_CLICKED, app);
    lv_label_set_text(lv_label_create(save), "Save & Connect");
#else
    paxx_create_nav_bar(screen_, "Printer", paxx_back_home_cb, app, app->isDark());

    const PrinterProfile &p = activeProfile(app->config());
    int y = 56;
    auto addField = [&](const char *ph, lv_obj_t **ta, const char *val, PaxxKbMode mode = PaxxKbMode::Text) {
        *ta = lv_textarea_create(screen_);
        lv_obj_set_width(*ta, LV_PCT(92));
        lv_obj_align(*ta, LV_ALIGN_TOP_MID, 0, y);
        y += 48;
        lv_textarea_set_placeholder_text(*ta, ph);
        if (val && val[0]) lv_textarea_set_text(*ta, val);
        PaxxKeyboard::attach(*ta, mode);
    };

    addField("Profile name", &nameTa_, p.name);
    addField("Printer IP", &hostTa_, p.host, PaxxKbMode::Number);
    addField("Moonraker port", &portTa_, String(p.moonrakerPort).c_str(), PaxxKbMode::Number);
    addField("API key (optional)", &keyTa_, p.apiKey);
    addField("Username (optional)", &userTa_, p.username);
    addField("Password (optional)", &passTa_, p.password, PaxxKbMode::Password);

    lv_obj_t *save = lv_btn_create(screen_);
    lv_obj_align(save, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_add_event_cb(save, [](lv_event_t *e) {
        auto *a = static_cast<PaxxApp *>(lv_event_get_user_data(e));
        PrinterProfile &prof = activeProfile(a->config());
        strlcpy(prof.name, lv_textarea_get_text(a->setup().nameInput()), sizeof(prof.name));
        strlcpy(prof.host, lv_textarea_get_text(a->setup().hostInput()), sizeof(prof.host));
        prof.moonrakerPort = static_cast<uint16_t>(atoi(lv_textarea_get_text(a->setup().portInput())));
        if (prof.moonrakerPort == 0) prof.moonrakerPort = 7125;
        strlcpy(prof.apiKey, lv_textarea_get_text(a->setup().keyInput()), sizeof(prof.apiKey));
        strlcpy(prof.username, lv_textarea_get_text(a->setup().userInput()), sizeof(prof.username));
        strlcpy(prof.password, lv_textarea_get_text(a->setup().passInput()), sizeof(prof.password));
        prof.useAuth = prof.username[0] != '\0';
        if (a->config().profileCount <= 0) a->config().profileCount = 1;
        a->saveConfig();
        a->applyProfile();
        a->showHome();
    }, LV_EVENT_CLICKED, app);
    lv_label_set_text(lv_label_create(save), "Save & Connect");
    advancedBtn_ = nullptr;
    advancedPanel_ = nullptr;
    hintLbl_ = nullptr;
    navBackBtn_ = nullptr;
#endif
}

void WifiScreen::updateNavBack() {
#if PAXX_REMOTE_ONLY
    if (!navBackBtn_) return;
    if (PaxxPreferences::instance().hasPrinter()) {
        lv_obj_clear_flag(navBackBtn_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(navBackBtn_, LV_OBJ_FLAG_HIDDEN);
    }
#else
    if (navBackBtn_) lv_obj_clear_flag(navBackBtn_, LV_OBJ_FLAG_HIDDEN);
#endif
}

void WifiScreen::create(PaxxApp *app, lv_obj_t *parent) {
    app_ = app;
    screen_ = lv_obj_create(parent);
    lv_obj_set_size(screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(screen_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_, 0, LV_PART_MAIN);
    paxx_style_form_screen(screen_);
#if PAXX_REMOTE_ONLY
    paxx_create_nav_bar(screen_, "WiFi Setup", paxx_back_remote_cb, app, app->isDark(), &navBackBtn_);
#else
    paxx_create_nav_bar(screen_, "WiFi", paxx_back_home_cb, app, app->isDark(), &navBackBtn_);
#endif

    statusLbl_ = lv_label_create(screen_);
    paxx_set_form_width(statusLbl_);
    lv_obj_align(statusLbl_, LV_ALIGN_TOP_MID, 0, 52);
    lv_label_set_long_mode(statusLbl_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(statusLbl_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(statusLbl_, "Scanning…");

    networkList_ = lv_list_create(screen_);
    paxx_set_form_width(networkList_);
    lv_obj_set_height(networkList_, 130);
    lv_obj_align(networkList_, LV_ALIGN_TOP_MID, 0, 78);

    passTa_ = lv_textarea_create(screen_);
    paxx_set_form_width(passTa_);
    lv_obj_align(passTa_, LV_ALIGN_TOP_MID, 0, 218);
    lv_textarea_set_password_mode(passTa_, true);
    lv_textarea_set_one_line(passTa_, true);
    lv_textarea_set_placeholder_text(passTa_, "WiFi password");
    PaxxKeyboard::attach(passTa_, PaxxKbMode::Password);

    lv_obj_t *connect = lv_btn_create(screen_);
    paxx_set_form_width(connect);
    lv_obj_align(connect, LV_ALIGN_TOP_MID, 0, 272);
    lv_obj_add_event_cb(connect, [](lv_event_t *e) {
        static_cast<PaxxApp *>(lv_event_get_user_data(e))->wifiScreen().connectSelected();
    }, LV_EVENT_CLICKED, app);
    lv_label_set_text(lv_label_create(connect), "Join Network");

    lv_obj_t *rescan = lv_btn_create(screen_);
    paxx_set_form_width(rescan);
    lv_obj_align(rescan, LV_ALIGN_TOP_MID, 0, 318);
    lv_obj_add_event_cb(rescan, [](lv_event_t *e) {
        if (PaxxKeyboard::isVisible()) PaxxKeyboard::hide();
        static_cast<PaxxApp *>(lv_event_get_user_data(e))->wifiScreen().scanNetworks();
    }, LV_EVENT_CLICKED, app);
    lv_label_set_text(lv_label_create(rescan), LV_SYMBOL_REFRESH " Scan again");

    lv_obj_t *forget = lv_btn_create(screen_);
    paxx_set_form_width(forget);
    lv_obj_align(forget, LV_ALIGN_TOP_MID, 0, 364);
    lv_obj_set_style_bg_color(forget, PaxxTheme::danger(), LV_PART_MAIN);
    lv_obj_add_event_cb(forget, [](lv_event_t *e) {
        PaxxKeyboard::hide();
        static_cast<PaxxApp *>(lv_event_get_user_data(e))->wifiScreen().forgetAllNetworks();
    }, LV_EVENT_CLICKED, app);
    lv_label_set_text(lv_label_create(forget), "Forget All Networks");
}

void WifiScreen::setStatus(const char *text) {
    if (!statusLbl_) return;
    const char *msg = text ? text : "";
    const char *cur = lv_label_get_text(statusLbl_);
    if (cur && strcmp(cur, msg) == 0) return;
    lv_label_set_text(statusLbl_, msg);
}

void WifiScreen::selectNetwork(size_t index) {
    if (index >= networks_.size()) return;
    selectedIndex_ = static_cast<int>(index);
    const WifiNetwork &net = networks_[index];

    if (!net.secure) {
        PaxxKeyboard::hide();
        strlcpy(app_->config().wifi.ssid, net.ssid, sizeof(app_->config().wifi.ssid));
        app_->config().wifi.password[0] = '\0';
        app_->saveConfig();
        setStatus("Connecting…");
        app_->showGlobalLoading(true, "Connecting to WiFi…");
        app_->wifi().startConnect(net.ssid, "", 15);
        return;
    }

    lv_textarea_set_text(passTa_, "");
    char hint[48];
    snprintf(hint, sizeof(hint), "Password for %s", net.ssid);
    lv_textarea_set_placeholder_text(passTa_, hint);
    setStatus("Enter password, then tap Join Network");
    PaxxKeyboard::promptFor(passTa_);
}

void WifiScreen::connectSelected() {
    if (selectedIndex_ < 0 || static_cast<size_t>(selectedIndex_) >= networks_.size()) {
        setStatus("Tap a network first");
        return;
    }
    const WifiNetwork &net = networks_[static_cast<size_t>(selectedIndex_)];
    PaxxKeyboard::hide();
    strlcpy(app_->config().wifi.ssid, net.ssid, sizeof(app_->config().wifi.ssid));
    strlcpy(app_->config().wifi.password, lv_textarea_get_text(passTa_), sizeof(app_->config().wifi.password));
    app_->saveConfig();
    setStatus("Connecting…");
    app_->showGlobalLoading(true, "Connecting to WiFi…");
    app_->wifi().startConnect(app_->config().wifi.ssid, app_->config().wifi.password, 15);
}

void WifiScreen::forgetAllNetworks() {
    Serial.println("[WiFi UI] Forget all networks");
    app_->config().wifi.ssid[0] = '\0';
    app_->config().wifi.password[0] = '\0';
    app_->saveConfig();
    app_->wifi().forgetAll();

    selectedIndex_ = -1;
    networks_.clear();
    lv_obj_clean(networkList_);
    lv_textarea_set_text(passTa_, "");
    lv_textarea_set_placeholder_text(passTa_, "WiFi password");
    setStatus("Networks cleared — scanning…");
    scanNetworks();
    PaxxNotify::show("WiFi", "Saved networks cleared");
}

void WifiScreen::onEnter() {
    updateNavBack();
    selectedIndex_ = -1;
    lv_textarea_set_text(passTa_, "");
    lv_textarea_set_placeholder_text(passTa_, "WiFi password");
    if (WiFi.isConnected()) {
        setStatus(WiFi.localIP().toString().c_str());
    } else {
        setStatus("Scanning…");
    }
    scanNetworks();
}

void WifiScreen::scanNetworks() {
    Serial.println("[WiFi UI] scan start");
    scanning_ = true;
    app_->showGlobalLoading(true, "Scanning WiFi…");
    setStatus("Scanning…");

    std::vector<WifiNetwork> nets;
    app_->wifi().scan(nets);
    scanning_ = false;
    app_->showGlobalLoading(false);

    auto ensureListed = [&](const char *ssid, int32_t rssi, bool secure) {
        if (!ssid || !ssid[0]) return;
        for (const auto &n : nets) {
            if (strcmp(n.ssid, ssid) == 0) return;
        }
        WifiNetwork net{};
        strlcpy(net.ssid, ssid, sizeof(net.ssid));
        net.rssi = rssi;
        net.secure = secure;
        nets.push_back(net);
    };

    ensureListed(app_->config().wifi.ssid, -55, true);
    if (WiFi.isConnected()) {
        ensureListed(WiFi.SSID().c_str(), WiFi.RSSI(), true);
    }

    applyNetworkList(nets);
}

void WifiScreen::applyNetworkList(const std::vector<WifiNetwork> &nets) {
    std::vector<WifiNetwork> filtered;
    for (const auto &net : nets) {
        if (!net.ssid[0]) continue;
        bool found = false;
        for (auto &existing : filtered) {
            if (strcmp(existing.ssid, net.ssid) == 0) {
                if (net.rssi > existing.rssi) existing = net;
                found = true;
                break;
            }
        }
        if (!found) filtered.push_back(net);
    }

    networks_ = filtered;
    selectedIndex_ = -1;
    lv_obj_clean(networkList_);

    Serial.printf("[WiFi UI] populating list with %u network(s)\n", static_cast<unsigned>(filtered.size()));

    if (filtered.empty()) {
        lv_list_add_text(networkList_, "No networks found");
        setStatus("No networks found — tap Scan again");
        lv_refr_now(NULL);
        return;
    }

    for (size_t i = 0; i < filtered.size(); ++i) {
        char label[64];
        snprintf(label, sizeof(label), "%s%s  (%d dBm)",
                 filtered[i].secure ? LV_SYMBOL_EYE_CLOSE " " : "",
                 filtered[i].ssid, filtered[i].rssi);
        lv_obj_t *btn = lv_list_add_button(networkList_, LV_SYMBOL_WIFI, label);
        lv_obj_set_user_data(btn, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(btn, [](lv_event_t *e) {
            auto *self = static_cast<WifiScreen *>(lv_event_get_user_data(e));
            auto *target = static_cast<lv_obj_t *>(lv_event_get_target(e));
            const size_t idx =
                static_cast<size_t>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(target)));
            self->selectNetwork(idx);
        }, LV_EVENT_CLICKED, this);
    }

    char status[56];
    snprintf(status, sizeof(status), "Found %u — tap a network", static_cast<unsigned>(filtered.size()));
    setStatus(status);
    lv_refr_now(NULL);
}
