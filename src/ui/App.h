#pragma once

#include <lvgl.h>
#include <vector>
#include "moonraker/MoonrakerClient.h"
#include "moonraker/MoonrakerRest.h"
#include "paxx/RemoteScreen.h"
#include "paxx/CameraService.h"
#include "paxx/ThumbnailLoader.h"
#include "storage/Preferences.h"
#include "net/WifiService.h"
#include "net/OtaService.h"

class PaxxApp;

class HomeScreen {
public:
    void create(PaxxApp *app, lv_obj_t *parent);
    void update(const PrinterStatus &status);
    void onTick();
    lv_obj_t *root() const { return screen_; }
private:
    void releasePreview();
    void syncPreview(const PrinterStatus &status);
    void applyReadyPreview();

    PaxxApp *app_ = nullptr;
    lv_obj_t *screen_ = nullptr;
    lv_obj_t *card_ = nullptr;
    lv_obj_t *previewFrame_ = nullptr;
    lv_obj_t *previewImage_ = nullptr;
    lv_obj_t *infoCol_ = nullptr;
    lv_obj_t *connChip_ = nullptr;
    lv_obj_t *filenameLbl_ = nullptr;
    lv_obj_t *stateLbl_ = nullptr;
    lv_obj_t *progressBar_ = nullptr;
    lv_obj_t *progressLbl_ = nullptr;
    lv_obj_t *tempLbl_ = nullptr;
    lv_image_dsc_t previewDsc_{};
    void *previewBuf_ = nullptr;
    lv_color_format_t previewFormat_ = LV_COLOR_FORMAT_UNKNOWN;
    ConnectionState lastConnection_ = ConnectionState::Disconnected;
    char lastFilename_[96] = {};
    char lastThumbRequest_[96] = {};
    char lastShownThumb_[96] = {};
    PrintState lastPrintState_ = PrintState::Unknown;
    int lastProgress_ = -1;
    int lastActiveTool_ = -1;
    int lastNozzleTemp_ = -1;
    int lastNozzleTarget_ = -1;
    int lastBedTemp_ = -1;
    int lastBedTarget_ = -1;
};

class PrintScreen {
public:
    void create(PaxxApp *app, lv_obj_t *parent);
    void update(const PrinterStatus &status);
    lv_obj_t *root() const { return screen_; }
private:
    PaxxApp *app_ = nullptr;
    lv_obj_t *screen_ = nullptr;
    lv_obj_t *detailsLbl_ = nullptr;
    lv_obj_t *messageLbl_ = nullptr;
    lv_obj_t *speedSlider_ = nullptr;
    lv_obj_t *flowSlider_ = nullptr;
};

class FilamentScreen {
public:
    void create(PaxxApp *app, lv_obj_t *parent);
    void update(const PrinterStatus &status);
    void onEnter();
    lv_obj_t *root() const { return screen_; }
    void setHint(const char *text);
    int &editSlot() { return editSlot_; }
    lv_obj_t *materialInput() const { return materialTa_; }
    const char *selectedColor() const { return selectedColor_; }
    void setSelectedColor(const char *hex);
    void updateColorPreview();
private:
    void rebuildGrid(const PrinterStatus &status);
    bool filamentsChanged(const std::vector<FilamentSlot> &filaments) const;
    void buildColorPicker();

    PaxxApp *app_ = nullptr;
    lv_obj_t *screen_ = nullptr;
    lv_obj_t *grid_ = nullptr;
    lv_obj_t *hintLbl_ = nullptr;
    lv_obj_t *materialTa_ = nullptr;
    lv_obj_t *colorSwatch_ = nullptr;
    lv_obj_t *colorGrid_ = nullptr;
    char selectedColor_[16] = "#888888";
    int editSlot_ = 0;
    std::vector<FilamentSlot> lastFilaments_;
};

class RemoteScreenView {
public:
    void create(PaxxApp *app, lv_obj_t *parent);
    void onEnter();
    void onLeave();
    void onTick();
    lv_obj_t *root() const { return screen_; }
private:
    void releaseFrame();
    void updateStatusLine(const char *text);
    void setLoadingVisible(bool visible, const char *text = nullptr);
    void showTouchIndicator(lv_point_t pt);
    void handleCanvasTouch(lv_event_t *e);

    PaxxApp *app_ = nullptr;
    lv_obj_t *screen_ = nullptr;
    lv_obj_t *canvasArea_ = nullptr;
    lv_obj_t *image_ = nullptr;
    lv_obj_t *loadingArc_ = nullptr;
    lv_obj_t *touchMarker_ = nullptr;
    lv_obj_t *statusLbl_ = nullptr;
    lv_image_dsc_t imageDsc_{};
    uint8_t *frameBuf_ = nullptr;
    lv_color_format_t frameFormat_ = LV_COLOR_FORMAT_UNKNOWN;
    int frameW_ = 0;
    int frameH_ = 0;
    int lastSentU1X_ = -1;
    int lastSentU1Y_ = -1;
    unsigned long lastFetchMs_ = 0;
    unsigned long lastProbeMs_ = 0;
    unsigned long connectStartedMs_ = 0;
    unsigned long lastTouchSendMs_ = 0;
    unsigned long lastTouchActivityMs_ = 0;
    unsigned long lastBlitMs_ = 0;
    unsigned long touchMarkerHideMs_ = 0;
    bool fetchInProgress_ = false;
    bool serviceAvailable_ = false;
    int failCount_ = 0;
};

class TimelapseScreen {
public:
    void create(PaxxApp *app, lv_obj_t *parent);
    void onEnter();
    lv_obj_t *root() const { return screen_; }
    void refreshList();
private:
    struct TimelapseCtx {
        PaxxApp *app;
        TimelapseEntry entry;
    };

    PaxxApp *app_ = nullptr;
    lv_obj_t *screen_ = nullptr;
    lv_obj_t *list_ = nullptr;
    lv_obj_t *detailLbl_ = nullptr;
    std::vector<TimelapseCtx> timelapseCtxs_;
};

class CameraScreen {
public:
    void create(PaxxApp *app, lv_obj_t *parent);
    void onEnter();
    void onLeave();
    void onTick();
    lv_obj_t *root() const { return screen_; }
private:
    void releaseFrame();

    PaxxApp *app_ = nullptr;
    lv_obj_t *screen_ = nullptr;
    lv_obj_t *image_ = nullptr;
    lv_obj_t *statusLbl_ = nullptr;
    lv_image_dsc_t imageDsc_{};
    uint16_t *frameBuf_ = nullptr;
    unsigned long lastFetchMs_ = 0;
    unsigned long lastRequestMs_ = 0;
};

class PrintPrepareScreen {
public:
    void create(PaxxApp *app, lv_obj_t *parent);
    void open(const char *gcodePath);
    lv_obj_t *root() const { return screen_; }

private:
    struct ColorRow {
        lv_obj_t *toolBtns[4] = {};
    };

    void rebuildRows();
    void setToolForColor(int colorIndex, int tool);
    void autoMapColors();
    void startPrintJob();
    static void onToolPick(lv_event_t *e);

    PaxxApp *app_ = nullptr;
    lv_obj_t *screen_ = nullptr;
    lv_obj_t *titleLbl_ = nullptr;
    lv_obj_t *metaLbl_ = nullptr;
    lv_obj_t *rowsPanel_ = nullptr;
    lv_obj_t *hintLbl_ = nullptr;
    char gcodePath_[128] = {};
    GcodeMetadata meta_{};
    int toolMap_[4] = {0, 1, 2, 3};
    ColorRow rows_[4];
};

class FilesScreen {
public:
    void create(PaxxApp *app, lv_obj_t *parent);
    void onEnter();
    lv_obj_t *root() const { return screen_; }
    void refreshList();
private:
    struct FileCtx {
        PaxxApp *app;
        char path[128];
    };

    PaxxApp *app_ = nullptr;
    lv_obj_t *screen_ = nullptr;
    lv_obj_t *list_ = nullptr;
    lv_obj_t *statusLbl_ = nullptr;
    std::vector<FileCtx> fileCtxs_;
};

class TerminalScreen {
public:
    void create(PaxxApp *app, lv_obj_t *parent);
    void onEnter();
    void onTick();
    lv_obj_t *root() const { return screen_; }
    lv_obj_t *cmdInput() const { return cmdTa_; }
private:
    PaxxApp *app_ = nullptr;
    lv_obj_t *screen_ = nullptr;
    lv_obj_t *logTa_ = nullptr;
    lv_obj_t *cmdTa_ = nullptr;
    unsigned long lastRefreshMs_ = 0;
};

class ControlsScreen {
public:
    void create(PaxxApp *app, lv_obj_t *parent);
    void update(const PrinterStatus &status);
    lv_obj_t *root() const { return screen_; }
private:
    PaxxApp *app_ = nullptr;
    lv_obj_t *screen_ = nullptr;
    lv_obj_t *statusLbl_ = nullptr;
    lv_obj_t *speedSlider_ = nullptr;
    lv_obj_t *flowSlider_ = nullptr;
    lv_obj_t *fanSlider_ = nullptr;
    int lastSpeed_ = -1;
    int lastFlow_ = -1;
};

class SettingsScreen {
public:
    void create(PaxxApp *app, lv_obj_t *parent);
    void setHint(const char *text);
    lv_obj_t *root() const { return screen_; }
private:
    PaxxApp *app_ = nullptr;
    lv_obj_t *screen_ = nullptr;
    lv_obj_t *hintLbl_ = nullptr;
};

class SetupScreen {
public:
    void create(PaxxApp *app, lv_obj_t *parent);
    void onEnter();
    lv_obj_t *root() const { return screen_; }
    lv_obj_t *hostInput() const { return hostTa_; }
    lv_obj_t *portInput() const { return portTa_; }
    lv_obj_t *userInput() const { return userTa_; }
    lv_obj_t *passInput() const { return passTa_; }
    lv_obj_t *keyInput() const { return keyTa_; }
    lv_obj_t *nameInput() const { return nameTa_; }
private:
    void updateNavBack();
    void toggleAdvanced();

    PaxxApp *app_ = nullptr;
    lv_obj_t *screen_ = nullptr;
    lv_obj_t *navBackBtn_ = nullptr;
    lv_obj_t *hostTa_ = nullptr;
    lv_obj_t *portTa_ = nullptr;
    lv_obj_t *userTa_ = nullptr;
    lv_obj_t *passTa_ = nullptr;
    lv_obj_t *keyTa_ = nullptr;
    lv_obj_t *nameTa_ = nullptr;
    lv_obj_t *advancedBtn_ = nullptr;
    lv_obj_t *advancedPanel_ = nullptr;
    lv_obj_t *hintLbl_ = nullptr;
    bool advancedVisible_ = false;
};

class WifiScreen {
public:
    void create(PaxxApp *app, lv_obj_t *parent);
    void onEnter();
    void scanNetworks();
    void forgetAllNetworks();
    void setStatus(const char *text);
    lv_obj_t *passInput() const { return passTa_; }
    lv_obj_t *root() const { return screen_; }
private:
    void applyNetworkList(const std::vector<WifiNetwork> &nets);
    void selectNetwork(size_t index);
    void connectSelected();
    void updateNavBack();

    PaxxApp *app_ = nullptr;
    lv_obj_t *screen_ = nullptr;
    lv_obj_t *navBackBtn_ = nullptr;
    lv_obj_t *networkList_ = nullptr;
    lv_obj_t *passTa_ = nullptr;
    lv_obj_t *statusLbl_ = nullptr;
    std::vector<WifiNetwork> networks_;
    int selectedIndex_ = -1;
};

class PaxxApp {
public:
    void begin();
    void loop();

    MoonrakerClient &moonraker() { return moonraker_; }
    MoonrakerRest &moonrakerRest() { return moonrakerRest_; }
    RemoteScreenClient &remoteScreen() { return remoteScreen_; }
    CameraService &camera() { return camera_; }
    ThumbnailLoader &thumbnails() { return thumbnails_; }
    WifiService &wifi() { return wifi_; }
    AppConfig &config() { return config_; }
    bool isDark() const { return config_.darkTheme; }

    void saveConfig();
    void applyProfile();
    void reconnectPrinter();
    void syncServices();
    void ensureMoonrakerRest();
    bool sendGcode(const char *script);

    void showHome();
    void showPrint();
    void showFilament();
    void showRemote();
    void showTimelapse();
    void showCamera();
    void showFiles();
    void showPrintPrepare(const char *gcodePath);
    void showControls();
    void showTerminal();
    void showSettings();
    void showSetup();
    void showWifi();

    void onStatusUpdate(const PrinterStatus &status);
    void refreshActiveScreen(const PrinterStatus &status);
    lv_obj_t *createMenuButton(lv_obj_t *parent, const char *icon, const char *label, lv_event_cb_t cb);

    HomeScreen &home() { return home_; }
    SettingsScreen &settings() { return settings_; }
    SetupScreen &setup() { return setup_; }
    WifiScreen &wifiScreen() { return wifiScreen_; }
    FilamentScreen &filament() { return filament_; }
    TerminalScreen &terminal() { return terminal_; }
    TimelapseScreen &timelapse() { return timelapse_; }
    FilesScreen &files() { return files_; }
    PrintPrepareScreen &printPrepare() { return printPrepare_; }

private:
    void buildShell();
    void buildGearMenu();
    void hideGearMenu();
    void toggleGearMenu();
    void showScreen(lv_obj_t *screen, const char *tickKind = nullptr);

    AppConfig config_{};
    MoonrakerClient moonraker_;
    MoonrakerRest moonrakerRest_;
    RemoteScreenClient remoteScreen_;
    CameraService camera_;
    ThumbnailLoader thumbnails_;
    WifiService wifi_;
    OtaService ota_;

    lv_obj_t *shell_ = nullptr;
    lv_obj_t *content_ = nullptr;
    lv_obj_t *gearBtn_ = nullptr;
    lv_obj_t *gearMenu_ = nullptr;
    lv_obj_t *activeScreen_ = nullptr;

    static void onKeyboardVisibility(bool visible, void *userData);
    const char *activeTickKind_ = nullptr;
    unsigned long wifiLostAtMs_ = 0;

    HomeScreen home_;
    PrintScreen print_;
    FilamentScreen filament_;
    RemoteScreenView remote_;
    TimelapseScreen timelapse_;
    CameraScreen cameraScreen_;
    FilesScreen files_;
    PrintPrepareScreen printPrepare_;
    ControlsScreen controls_;
    TerminalScreen terminal_;
    SettingsScreen settings_;
    SetupScreen setup_;
    WifiScreen wifiScreen_;
};

void paxx_back_home_cb(lv_event_t *e);
void paxx_back_files_cb(lv_event_t *e);
