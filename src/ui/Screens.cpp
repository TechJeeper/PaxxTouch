#include "ui/App.h"
#include "ui/Theme.h"
#include "ui/Notify.h"
#include "ui/Keyboard.h"
#include "ui/ConsoleLog.h"
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
    static_cast<PaxxApp *>(lv_event_get_user_data(e))->showHome();
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

    makeMenuBtn(app, menu, LV_SYMBOL_PLAY, "Print", [](lv_event_t *e) { static_cast<PaxxApp *>(lv_event_get_user_data(e))->showPrint(); });
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

    const int progress = static_cast<int>(status.progress + 0.5f);
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

    detailsLbl_ = lv_label_create(screen_);
    lv_obj_align(detailsLbl_, LV_ALIGN_TOP_LEFT, 8, 56);
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
        lv_obj_t *lbl = lv_label_create(card);
        lv_label_set_text_fmt(lbl, "T%d  %s\n%s  %s\nmap T%d",
                              index, loaded ? "present" : "empty",
                              material && material[0] ? material : "(none)",
                              color && color[0] ? color : "(no color)", mappedTool);
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
                lv_textarea_set_text(self->colorTa_, f.color);
                break;
            }
            char buf[32];
            snprintf(buf, sizeof(buf), "Editing T%d — update material/color below", tool);
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
    lv_obj_set_size(grid_, LV_PCT(96), 180);
    lv_obj_set_style_bg_opa(grid_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(grid_, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(grid_, LV_FLEX_FLOW_ROW_WRAP);

    materialTa_ = lv_textarea_create(screen_);
    lv_obj_set_width(materialTa_, 180);
    lv_obj_align(materialTa_, LV_ALIGN_BOTTOM_LEFT, 8, -80);
    lv_textarea_set_placeholder_text(materialTa_, "Material");
    PaxxKeyboard::attach(materialTa_);
    colorTa_ = lv_textarea_create(screen_);
    lv_obj_set_width(colorTa_, 120);
    lv_obj_align(colorTa_, LV_ALIGN_BOTTOM_LEFT, 200, -80);
    lv_textarea_set_placeholder_text(colorTa_, "Color");
    PaxxKeyboard::attach(colorTa_);

    lv_obj_t *saveBtn = lv_btn_create(screen_);
    lv_obj_align(saveBtn, LV_ALIGN_BOTTOM_RIGHT, -8, -80);
    lv_obj_add_event_cb(saveBtn, [](lv_event_t *e) {
        auto *a = static_cast<PaxxApp *>(lv_event_get_user_data(e));
        a->ensureMoonrakerRest();
        if (a->moonrakerRest().setFilamentSlot(a->filament().editSlot(),
                lv_textarea_get_text(a->filament().materialInput()),
                lv_textarea_get_text(a->filament().colorInput()))) {
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
    paxx_create_nav_bar(screen_, "Remote Screen", paxx_back_home_cb, app, app->isDark());

    canvasArea_ = lv_obj_create(screen_);
    lv_obj_align(canvasArea_, LV_ALIGN_TOP_LEFT, 0, kNavBarHeight);
    lv_obj_set_size(canvasArea_, PT_LCD_H_RES, PT_LCD_V_RES - kNavBarHeight);
    lv_obj_set_style_bg_color(canvasArea_, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(canvasArea_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(canvasArea_, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(canvasArea_, 0, LV_PART_MAIN);
    lv_obj_add_flag(canvasArea_, LV_OBJ_FLAG_CLICKABLE);

    statusLbl_ = lv_label_create(canvasArea_);
    lv_obj_align(statusLbl_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_width(statusLbl_, LV_PCT(95));
    lv_label_set_long_mode(statusLbl_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(statusLbl_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    image_ = lv_image_create(canvasArea_);
    lv_obj_center(image_);

    lv_obj_add_event_cb(canvasArea_, [](lv_event_t *e) {
        static_cast<RemoteScreenView *>(lv_event_get_user_data(e))->handleCanvasTouch(e);
    }, LV_EVENT_ALL, this);
}

void RemoteScreenView::handleCanvasTouch(lv_event_t *e) {
    const lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED) return;

    const unsigned long now = millis();
    lastTouchActivityMs_ = now;

    lv_point_t pt;
    lv_indev_get_point(lv_indev_active(), &pt);
    lv_area_t area;
    lv_obj_get_coords(canvasArea_, &area);
    if (pt.x < area.x1 || pt.x > area.x2 || pt.y < area.y1 || pt.y > area.y2) return;

    const int areaW = lv_obj_get_width(canvasArea_);
    const int areaH = lv_obj_get_height(canvasArea_);
    const int imgW = frameW_ > 0 ? frameW_ : RemoteScreenClient::U1_WIDTH;
    const int imgH = frameH_ > 0 ? frameH_ : RemoteScreenClient::U1_HEIGHT;
    const ImageFit fit = fitImageInArea(imgW, imgH, areaW, areaH);
    if (fit.dispW <= 0 || fit.dispH <= 0) return;

    const int localX = pt.x - area.x1 - fit.offsetX;
    const int localY = pt.y - area.y1 - fit.offsetY;
    if (localX < 0 || localY < 0 || localX >= fit.dispW || localY >= fit.dispH) return;

    const int u1x = map(localX, 0, fit.dispW - 1, 0, imgW - 1);
    const int u1y = map(localY, 0, fit.dispH - 1, 0, imgH - 1);

    RemoteTouchAction action = RemoteTouchAction::Move;
    if (code == LV_EVENT_PRESSED) {
        action = RemoteTouchAction::Down;
        lastSentU1X_ = u1x;
        lastSentU1Y_ = u1y;
    } else if (code == LV_EVENT_RELEASED) {
        action = RemoteTouchAction::Up;
        lastSentU1X_ = u1x;
        lastSentU1Y_ = u1y;
    } else {
        if (now - lastTouchSendMs_ < 80) return;
        if (lastSentU1X_ >= 0 && abs(u1x - lastSentU1X_) < 10 && abs(u1y - lastSentU1Y_) < 10) return;
        lastSentU1X_ = u1x;
        lastSentU1Y_ = u1y;
    }

    lastTouchSendMs_ = now;
    app_->remoteScreen().queueTouch(u1x, u1y, action);
}

void RemoteScreenView::updateStatusLine(const char *text) {
    if (!statusLbl_) return;
    const bool show = text && text[0];
    lv_label_set_text(statusLbl_, show ? text : "");
    if (show) {
        lv_obj_move_foreground(statusLbl_);
        lv_obj_clear_flag(statusLbl_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(statusLbl_, LV_OBJ_FLAG_HIDDEN);
    }
}

void RemoteScreenView::onEnter() {
    lastFetchMs_ = 0;
    lastProbeMs_ = 0;
    lastTouchSendMs_ = 0;
    lastTouchActivityMs_ = 0;
    lastSentU1X_ = -1;
    lastSentU1Y_ = -1;
    serviceAvailable_ = false;

    if (!app_->config().remoteScreenEnabled) {
        updateStatusLine("Enable Remote Screen in Settings");
        return;
    }
    if (!WiFi.isConnected()) {
        updateStatusLine("WiFi required for remote screen");
        return;
    }

    app_->syncServices();
    app_->remoteScreen().resetProbe();
    updateStatusLine("Connecting to U1 remote screen…");
}

void RemoteScreenView::onLeave() {
    releaseFrame();
}

void RemoteScreenView::releaseFrame() {
    if (image_) lv_image_set_src(image_, NULL);
    if (frameBuf_) {
        ImageDecoder::freeBuffer(frameBuf_);
        frameBuf_ = nullptr;
    }
    frameW_ = 0;
    frameH_ = 0;
    frameFormat_ = LV_COLOR_FORMAT_UNKNOWN;
    fetchInProgress_ = false;
}

void RemoteScreenView::onTick() {
    if (!app_->config().remoteScreenEnabled) return;
    if (!WiFi.isConnected()) return;

    const RemoteProbeState probe = app_->remoteScreen().probeState();
    if (probe == RemoteProbeState::Running) {
        updateStatusLine("Connecting to U1 remote screen…");
        return;
    }
    if (probe == RemoteProbeState::Idle) {
        app_->remoteScreen().resetProbe();
        updateStatusLine("Connecting to U1 remote screen…");
        return;
    }
    if (probe == RemoteProbeState::Failed) {
        updateStatusLine(app_->remoteScreen().probeError());
        if (millis() - lastProbeMs_ > 8000) {
            lastProbeMs_ = millis();
            app_->remoteScreen().resetProbe();
        }
        return;
    }

    serviceAvailable_ = true;
    app_->remoteScreen().setRefreshIntervalMs(
        (millis() - lastTouchActivityMs_ < 2500) ? 1500UL : 2000UL);
    app_->remoteScreen().pumpSnapshot();

    uint8_t *buf = nullptr;
    lv_color_format_t format = LV_COLOR_FORMAT_UNKNOWN;
    int w = 0;
    int h = 0;
    if (!app_->remoteScreen().pollFrame(buf, format, w, h)) {
        if (millis() - lastFetchMs_ > 6000 && lastFetchMs_ != 0) {
            updateStatusLine("Waiting for remote screen frames…");
        } else if (lastFetchMs_ == 0) {
            updateStatusLine("");
        }
        return;
    }

    if (buf) {
        if (image_) lv_image_set_src(image_, NULL);
        if (frameBuf_) ImageDecoder::freeBuffer(frameBuf_);
        frameBuf_ = buf;
        frameFormat_ = format;
        frameW_ = w;
        frameH_ = h;
        ImageDecoder::bindLvImage(imageDsc_, image_, frameBuf_, frameFormat_, w, h,
                                  lv_obj_get_width(canvasArea_), lv_obj_get_height(canvasArea_));
        updateStatusLine("");
        lastFetchMs_ = millis();
        failCount_ = 0;
        Serial.printf("[Remote] frame displayed %dx%d\n", w, h);
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
    app_->syncServices();
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
    fetchInProgress_ = false;
}

void CameraScreen::onTick() {
    if (fetchInProgress_) return;
    if (millis() - lastFetchMs_ < 800) return;
    lastFetchMs_ = millis();
    fetchInProgress_ = true;

    int w = 0, h = 0;
    uint16_t *buf = nullptr;
    if (app_->camera().fetchSnapshot(buf, w, h)) {
        if (image_) lv_image_set_src(image_, NULL);
        if (frameBuf_) ImageDecoder::freeBuffer(frameBuf_);
        frameBuf_ = buf;
        ImageDecoder::bindLvImage(imageDsc_, image_, buf, LV_COLOR_FORMAT_RGB565, w, h);
        lv_label_set_text_fmt(statusLbl_, "Camera %dx%d", w, h);
    } else {
        lv_label_set_text_fmt(statusLbl_,
            "Camera unavailable (HTTP %d)\nEnable paxx12 internal camera in firmware-config",
            app_->camera().lastHttpCode());
    }
    fetchInProgress_ = false;
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
    lv_label_set_text(statusLbl_, "Tap file to start print");
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
        for (const auto &f : files) {
            if (f.isDir) continue;
            const char *name = strrchr(f.path, '/');
            name = name ? name + 1 : f.path;
            if (!strstr(name, ".gcode") && !strstr(name, ".gco") && !strstr(name, ".GCODE")) continue;
            fileCtxs_.push_back({app_, {}});
            FileCtx &ctx = fileCtxs_.back();
            strlcpy(ctx.path, f.path, sizeof(ctx.path));
            lv_obj_t *btn = lv_list_add_button(list_, LV_SYMBOL_FILE, name);
            lv_obj_add_event_cb(btn, [](lv_event_t *e) {
                auto *ctx = static_cast<FileCtx *>(lv_event_get_user_data(e));
                ctx->app->ensureMoonrakerRest();
                if (ctx->app->moonrakerRest().startPrint("gcodes", ctx->path)) {
                    PaxxNotify::show("Print", "Print started");
                    ctx->app->showPrint();
                } else {
                    PaxxNotify::show("Print", "Failed to start print");
                }
            }, LV_EVENT_CLICKED, &ctx);
            if (++count >= 40) break;
        }
        lv_label_set_text_fmt(statusLbl_, "%d printable file(s)", count);
    });
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

    lv_obj_t *about = lv_label_create(screen_);
    lv_obj_align(about, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_label_set_text(about, "PaxxTouch v" PAXXTOUCH_VERSION);
}

void SetupScreen::create(PaxxApp *app, lv_obj_t *parent) {
    app_ = app;
    screen_ = lv_obj_create(parent);
    lv_obj_set_size(screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(screen_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_, 0, LV_PART_MAIN);
    lv_obj_add_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_bottom(screen_, 220, LV_PART_MAIN);
    paxx_create_nav_bar(screen_, "Printer", paxx_back_home_cb, app, app->isDark());

    const PrinterProfile &p = activeProfile(app->config());
    int y = 56;
    auto addField = [&](const char *ph, lv_obj_t **ta, const char *val, bool password = false) {
        *ta = lv_textarea_create(screen_);
        lv_obj_set_width(*ta, LV_PCT(92));
        lv_obj_align(*ta, LV_ALIGN_TOP_MID, 0, y);
        y += 48;
        lv_textarea_set_placeholder_text(*ta, ph);
        if (val && val[0]) lv_textarea_set_text(*ta, val);
        PaxxKeyboard::attach(*ta, password);
    };

    addField("Profile name", &nameTa_, p.name);
    addField("Printer IP", &hostTa_, p.host);
    addField("Moonraker port", &portTa_, String(p.moonrakerPort).c_str());
    addField("API key (optional)", &keyTa_, p.apiKey);
    addField("Username (optional)", &userTa_, p.username);
    addField("Password (optional)", &passTa_, p.password, true);

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
}

void WifiScreen::create(PaxxApp *app, lv_obj_t *parent) {
    app_ = app;
    screen_ = lv_obj_create(parent);
    lv_obj_set_size(screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(screen_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_, 0, LV_PART_MAIN);
    lv_obj_add_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_bottom(screen_, 220, LV_PART_MAIN);
    paxx_create_nav_bar(screen_, "WiFi", paxx_back_home_cb, app, app->isDark());

    networkRoller_ = lv_roller_create(screen_);
    lv_obj_set_width(networkRoller_, LV_PCT(92));
    lv_obj_set_height(networkRoller_, 100);
    lv_obj_align(networkRoller_, LV_ALIGN_TOP_MID, 0, 56);
    lv_roller_set_options(networkRoller_, networkOptions_, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(networkRoller_, 3);
    lv_obj_add_event_cb(networkRoller_, [](lv_event_t *e) {
        const lv_event_code_t code = lv_event_get_code(e);
        if (code != LV_EVENT_VALUE_CHANGED && code != LV_EVENT_RELEASED) return;
        static_cast<WifiScreen *>(lv_event_get_user_data(e))->promptPasswordForSelection();
    }, LV_EVENT_ALL, this);

    lv_obj_t *forget = lv_btn_create(screen_);
    lv_obj_set_size(forget, LV_PCT(92), 36);
    lv_obj_align(forget, LV_ALIGN_TOP_MID, 0, 162);
    lv_obj_set_style_bg_color(forget, PaxxTheme::danger(), LV_PART_MAIN);
    lv_obj_add_event_cb(forget, [](lv_event_t *e) {
        PaxxKeyboard::hide();
        static_cast<PaxxApp *>(lv_event_get_user_data(e))->wifiScreen().forgetAllNetworks();
    }, LV_EVENT_CLICKED, app);
    lv_label_set_text(lv_label_create(forget), "Forget All Networks");

    passTa_ = lv_textarea_create(screen_);
    lv_obj_set_width(passTa_, LV_PCT(92));
    lv_obj_align(passTa_, LV_ALIGN_TOP_MID, 0, 204);
    lv_textarea_set_placeholder_text(passTa_, "WiFi password");
    PaxxKeyboard::attach(passTa_, true);

    lv_obj_t *scan = lv_btn_create(screen_);
    lv_obj_align(scan, LV_ALIGN_TOP_MID, 0, 264);
    lv_obj_add_event_cb(scan, [](lv_event_t *e) {
        if (PaxxKeyboard::isVisible()) PaxxKeyboard::hide();
        static_cast<PaxxApp *>(lv_event_get_user_data(e))->wifiScreen().scanNetworks();
    }, LV_EVENT_CLICKED, app);
    lv_label_set_text(lv_label_create(scan), "Scan Networks");

    lv_obj_t *connect = lv_btn_create(screen_);
    lv_obj_align(connect, LV_ALIGN_TOP_MID, 0, 320);
    lv_obj_add_event_cb(connect, [](lv_event_t *e) {
        PaxxKeyboard::hide();
        auto *a = static_cast<PaxxApp *>(lv_event_get_user_data(e));
        Serial.println("[WiFi UI] Connect clicked");
        char ssid[33];
        lv_roller_get_selected_str(a->wifiScreen().networkRoller(), ssid, sizeof(ssid));
        if (!a->wifiScreen().isSelectableNetwork(ssid)) {
            a->wifiScreen().setStatus("Scan and pick a network first");
            return;
        }
        Serial.printf("[WiFi UI] selected ssid=\"%s\"\n", ssid);
        strlcpy(a->config().wifi.ssid, ssid, sizeof(a->config().wifi.ssid));
        strlcpy(a->config().wifi.password, lv_textarea_get_text(a->wifiScreen().passInput()), sizeof(a->config().wifi.password));
        a->saveConfig();
        a->wifiScreen().setStatus("Connecting...");
        a->wifi().startConnect(a->config().wifi.ssid, a->config().wifi.password, 15);
    }, LV_EVENT_CLICKED, app);
    lv_label_set_text(lv_label_create(connect), "Connect");

    statusLbl_ = lv_label_create(screen_);
    lv_obj_align(statusLbl_, LV_ALIGN_TOP_MID, 0, 376);
    lv_label_set_text(statusLbl_, WiFi.isConnected() ? WiFi.localIP().toString().c_str() : "Not connected");
}

void WifiScreen::setStatus(const char *text) {
    if (statusLbl_) lv_label_set_text(statusLbl_, text ? text : "");
}

bool WifiScreen::isSelectableNetwork(const char *ssid) const {
    if (!ssid || !ssid[0]) return false;
    if (ssid[0] == '(') return false;
    if (strcmp(ssid, "Tap Scan Networks") == 0) return false;
    return true;
}

void WifiScreen::promptPasswordForSelection() {
    char ssid[33];
    lv_roller_get_selected_str(networkRoller_, ssid, sizeof(ssid));
    if (!isSelectableNetwork(ssid)) return;

    lv_textarea_set_text(passTa_, "");
    char hint[48];
    snprintf(hint, sizeof(hint), "Password for %s", ssid);
    lv_textarea_set_placeholder_text(passTa_, hint);
    setStatus("Enter password, then tap Connect");
    PaxxKeyboard::promptFor(passTa_);
}

void WifiScreen::forgetAllNetworks() {
    Serial.println("[WiFi UI] Forget all networks");
    app_->config().wifi.ssid[0] = '\0';
    app_->config().wifi.password[0] = '\0';
    app_->saveConfig();
    app_->wifi().forgetAll();

    lv_textarea_set_text(passTa_, "");
    lv_textarea_set_placeholder_text(passTa_, "WiFi password");
    strlcpy(networkOptions_, "Tap Scan Networks", sizeof(networkOptions_));
    lv_roller_set_options(networkRoller_, networkOptions_, LV_ROLLER_MODE_NORMAL);

    setStatus("All networks forgotten — scan to pick one");
    PaxxNotify::show("WiFi", "Saved networks cleared");
}

void WifiScreen::onEnter() {
    if (WiFi.isConnected()) {
        setStatus(WiFi.localIP().toString().c_str());
    } else {
        setStatus("Not connected");
    }
    lv_textarea_set_text(passTa_, "");
    if (app_->config().wifi.ssid[0] && isSelectableNetwork(app_->config().wifi.ssid)) {
        strlcpy(networkOptions_, app_->config().wifi.ssid, sizeof(networkOptions_));
        lv_roller_set_options(networkRoller_, networkOptions_, LV_ROLLER_MODE_NORMAL);
    }
}

void WifiScreen::scanNetworks() {
    Serial.println("[WiFi UI] Scan Networks clicked");
    setStatus("Scanning…");

    std::vector<WifiNetwork> nets;
    app_->wifi().scan(nets);

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
        Serial.printf("[WiFi UI] added saved/current ssid=\"%s\"\n", ssid);
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

    Serial.printf("[WiFi UI] populating roller with %u unique network(s)\n",
                  static_cast<unsigned>(filtered.size()));

    if (filtered.empty()) {
        strlcpy(networkOptions_, "(no networks found)", sizeof(networkOptions_));
        lv_roller_set_options(networkRoller_, networkOptions_, LV_ROLLER_MODE_NORMAL);
        setStatus("No networks found — check serial log");
        lv_refr_now(NULL);
        return;
    }

    networkOptions_[0] = '\0';
    for (size_t i = 0; i < filtered.size(); ++i) {
        if (i) strlcat(networkOptions_, "\n", sizeof(networkOptions_));
        strlcat(networkOptions_, filtered[i].ssid, sizeof(networkOptions_));
    }

    lv_roller_set_options(networkRoller_, networkOptions_, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(networkRoller_, 0, LV_ANIM_OFF);

    char status[56];
    snprintf(status, sizeof(status), "Found %u — scroll to pick", static_cast<unsigned>(filtered.size()));
    setStatus(status);
    lv_refr_now(NULL);
}
