#include "moonraker/MoonrakerClient.h"
#include "moonraker/MoonrakerRest.h"

#ifndef PAXXTOUCH_VERSION
#define PAXXTOUCH_VERSION "0.1.9"
#endif

namespace {

PrintState mapPrintState(const char *state) {
    if (!state) return PrintState::Unknown;
    if (strcmp(state, "standby") == 0) return PrintState::Standby;
    if (strcmp(state, "printing") == 0) return PrintState::Printing;
    if (strcmp(state, "paused") == 0) return PrintState::Paused;
    if (strcmp(state, "complete") == 0) return PrintState::Complete;
    if (strcmp(state, "cancelled") == 0) return PrintState::Cancelled;
    if (strcmp(state, "error") == 0) return PrintState::Error;
    return PrintState::Unknown;
}

bool readProgressFraction(JsonVariant v, float &out01) {
    if (v.isNull()) return false;
    if (!v.is<float>() && !v.is<double>() && !v.is<int>()) return false;
    out01 = v.as<float>();
    if (out01 > 1.0f) out01 /= 100.0f;
    if (out01 < 0.0f) out01 = 0.0f;
    if (out01 > 1.0f) out01 = 1.0f;
    return true;
}

}  // namespace

void MoonrakerClient::resetProgressCache() {
    cachedDisplayProgress_ = -1.0f;
    cachedVsdProgress_ = -1.0f;
    cachedLayerProgress_ = -1.0f;
}

void MoonrakerClient::recomputeProgress() {
    if (status_.printState == PrintState::Complete) {
        status_.progress = 100.0f;
        return;
    }
    if (status_.printState == PrintState::Standby || status_.printState == PrintState::Cancelled ||
        status_.printState == PrintState::Error) {
        status_.progress = 0.0f;
        return;
    }

    float p = -1.0f;
    if (cachedDisplayProgress_ >= 0.0f) p = cachedDisplayProgress_;
    else if (cachedVsdProgress_ >= 0.0f) p = cachedVsdProgress_;
    else if (cachedLayerProgress_ >= 0.0f) p = cachedLayerProgress_;
    if (p >= 0.0f) {
        status_.progress = p * 100.0f;
        if (status_.progress > 100.0f) status_.progress = 100.0f;
    }
}

void MoonrakerClient::begin(const char *host, uint16_t port, const char *token, const char *apiKey) {
    strlcpy(host_, host, sizeof(host_));
    port_ = port;
    if (token) strlcpy(token_, token, sizeof(token_));
    else token_[0] = '\0';
    if (apiKey) strlcpy(apiKey_, apiKey, sizeof(apiKey_));
    else apiKey_[0] = '\0';
    identified_ = false;
    subscribeSent_ = false;
    identifySentMs_ = 0;
    restOnly_ = false;
    ws_.onEvent([this](WStype_t type, uint8_t *payload, size_t length) {
        onWebSocketEvent(type, payload, length);
    });
}

bool MoonrakerClient::isLinkedTo(const char *host, uint16_t port) const {
    return host && host_[0] && strcmp(host_, host) == 0 && port_ == port;
}

bool MoonrakerClient::isConnectedTo(const char *host, uint16_t port) const {
    return isLinkedTo(host, port) && status_.connection == ConnectionState::Connected;
}

void MoonrakerClient::connect() {
    if (host_[0] == '\0') {
        setConnection(ConnectionState::Error);
        return;
    }

    if (status_.connection == ConnectionState::Connected ||
        status_.connection == ConnectionState::Connecting) {
        return;
    }

    ws_.disconnect();
    setConnection(ConnectionState::Connecting);
    identified_ = false;
    subscribeSent_ = false;
    identifySentMs_ = 0;

    String path = "/websocket";
    if (token_[0] != '\0') {
        path += "?token=";
        path += token_;
    }

    if (apiKey_[0] != '\0') {
        String headers = String("X-Api-Key: ") + apiKey_;
        ws_.setExtraHeaders(headers.c_str());
    } else {
        ws_.setExtraHeaders(nullptr);
    }

    ws_.begin(host_, port_, path.c_str(), "");
    ws_.setReconnectInterval(5000);
    ws_.enableHeartbeat(15000, 3000, 2);
    connectStartMs_ = millis();
    Serial.printf("[Moonraker] WS connect %s:%u%s\n", host_, port_, path.c_str());
}

void MoonrakerClient::disconnect() {
    ws_.disconnect();
    identified_ = false;
    subscribeSent_ = false;
    identifySentMs_ = 0;
    restOnly_ = false;
    resetProgressCache();
    setConnection(ConnectionState::Disconnected);
}

void MoonrakerClient::loop() {
    ws_.loop();

    if (status_.connection == ConnectionState::Connecting &&
        connectStartMs_ != 0 && millis() - connectStartMs_ > 20000) {
        Serial.println("[Moonraker] connect timeout");
        ws_.disconnect();
        connectStartMs_ = 0;
        setConnection(ConnectionState::Error);
    }

    if (status_.connection == ConnectionState::Connected && !subscribeSent_ &&
        identifySentMs_ != 0 && !identified_ &&
        millis() - identifySentMs_ > 5000) {
        Serial.println("[Moonraker] identify timeout — subscribing");
        identified_ = true;
        refreshObjects();
    }
}

void MoonrakerClient::setConnection(ConnectionState state) {
    if (status_.connection == state) return;
    status_.connection = state;
    if (statusCb_) statusCb_(status_);
}

void MoonrakerClient::onWebSocketEvent(WStype_t type, uint8_t *payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            if (status_.connection != ConnectionState::Disconnected) {
                Serial.println("[Moonraker] disconnected");
            }
            identified_ = false;
            subscribeSent_ = false;
            identifySentMs_ = 0;
            setConnection(ConnectionState::Disconnected);
            break;
        case WStype_CONNECTED:
            Serial.println("[Moonraker] connected");
            connectStartMs_ = 0;
            restOnly_ = false;
            setConnection(ConnectionState::Connected);
            identifyConnection();
            break;
        case WStype_ERROR:
            if (payload && length) {
                Serial.printf("[Moonraker] WS error: %.*s\n", static_cast<int>(length),
                              reinterpret_cast<char *>(payload));
            } else {
                Serial.println("[Moonraker] WS error");
            }
            break;
        case WStype_TEXT:
            handleMessage(reinterpret_cast<char *>(payload), length);
            break;
        default:
            break;
    }
}

void MoonrakerClient::sendJsonRpc(const char *method, JsonDocument &params, int id) {
    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    doc["method"] = method;
    doc["id"] = id;
    doc["params"].set(params);
    String out;
    serializeJson(doc, out);
    ws_.sendTXT(out);
}

void MoonrakerClient::identifyConnection() {
    JsonDocument params;
    params["client_name"] = "PaxxTouch";
    params["version"] = PAXXTOUCH_VERSION;
    params["type"] = "display";
    params["url"] = "https://github.com/paxx12-snapmaker-u1/PaxxTouch";
    identifyRpcId_ = rpcId_++;
    identifySentMs_ = millis();
    sendJsonRpc("server.connection.identify", params, identifyRpcId_);
}

void MoonrakerClient::refreshObjects() {
    if (subscribeSent_) return;
    subscribeSent_ = true;

    JsonDocument params;
    JsonObject objects = params["objects"].to<JsonObject>();
    objects["print_stats"] = nullptr;
    objects["extruder"] = nullptr;
    objects["extruder1"] = nullptr;
    objects["extruder2"] = nullptr;
    objects["extruder3"] = nullptr;
    objects["heater_bed"] = nullptr;
    objects["gcode_move"] = nullptr;
    objects["display_status"] = nullptr;
    objects["virtual_sdcard"] = nullptr;
    objects["print_task_config"] = nullptr;

    sendJsonRpc("printer.objects.subscribe", params, rpcId_++);
}

void MoonrakerClient::handleRpcResponse(JsonDocument &doc) {
    const int id = doc["id"] | 0;

    if (doc["error"]) {
        const char *msg = doc["error"]["message"] | "unknown";
        Serial.printf("[Moonraker] rpc error id=%d: %s\n", id, msg);
        if (id == identifyRpcId_ && !identified_) {
            identified_ = true;
            refreshObjects();
        }
        return;
    }

    if (id == identifyRpcId_ && !identified_) {
        identified_ = true;
        refreshObjects();
        return;
    }

    if (doc["result"].is<JsonObject>()) {
        JsonObject result = doc["result"].as<JsonObject>();
        JsonObject st = result["status"].as<JsonObject>();
        if (!st.isNull()) {
            parseStatusUpdate(st);
            if (statusCb_) statusCb_(status_);
        }
    }
}

void MoonrakerClient::sendGcode(const char *script) {
    if (status_.connection != ConnectionState::Connected) return;
    JsonDocument params;
    params["script"] = script;
    sendJsonRpc("printer.gcode.script", params, rpcId_++);
}

void MoonrakerClient::pausePrint() { sendGcode("PAUSE"); }
void MoonrakerClient::resumePrint() { sendGcode("RESUME"); }
void MoonrakerClient::cancelPrint() { sendGcode("CANCEL_PRINT"); }

void MoonrakerClient::setActiveTool(int toolIndex) {
    if (toolIndex < 0 || toolIndex > 3) return;
    char cmd[8];
    snprintf(cmd, sizeof(cmd), "T%d", toolIndex);
    sendGcode(cmd);
}

void MoonrakerClient::setSpeedFactor(int percent) {
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "M220 S%d", percent);
    sendGcode(cmd);
    status_.speedFactor = static_cast<float>(percent);
}

void MoonrakerClient::setFlowFactor(int percent) {
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "M221 S%d", percent);
    sendGcode(cmd);
    status_.flowFactor = static_cast<float>(percent);
}

void MoonrakerClient::homeAll() { sendGcode("G28"); }
void MoonrakerClient::bedMesh() { sendGcode("BED_MESH_CALIBRATE"); }

void MoonrakerClient::parseExtruder(JsonObject &root, const char *name, int idx) {
    if (idx < 0 || idx > 3) return;
    JsonObject ext = root[name].as<JsonObject>();
    if (ext.isNull()) return;
    status_.extruderTemps[idx] = ext["temperature"] | status_.extruderTemps[idx];
    status_.extruderTargets[idx] = ext["target"] | status_.extruderTargets[idx];
}

void MoonrakerClient::parsePrintTaskConfig(JsonObject &cfg) {
    status_.filaments.clear();

    auto pushSlot = [&](int index, bool loaded, const char *material, const char *colorHex, int mappedTool) {
        FilamentSlot f{};
        f.index = index;
        f.loaded = loaded;
        strlcpy(f.material, material && material[0] ? material : "Unknown", sizeof(f.material));
        if (colorHex && colorHex[0]) {
            if (colorHex[0] == '#') strlcpy(f.color, colorHex, sizeof(f.color));
            else snprintf(f.color, sizeof(f.color), "#%.6s", colorHex);
        } else {
            strlcpy(f.color, "#888888", sizeof(f.color));
        }
        f.mappedTool = mappedTool;
        status_.filaments.push_back(f);
    };

    JsonArray slots = cfg["filament_slots"].as<JsonArray>();
    if (!slots.isNull()) {
        for (JsonObject slot : slots) {
            FilamentSlot f{};
            f.index = slot["index"] | static_cast<int>(status_.filaments.size());
            strlcpy(f.material, slot["material"] | "Unknown", sizeof(f.material));
            strlcpy(f.color, slot["color"] | "#888888", sizeof(f.color));
            f.remainingMm = slot["remaining_length"] | 0.0f;
            f.loaded = slot["loaded"] | false;
            f.mappedTool = slot["mapped_tool"] | f.index;
            status_.filaments.push_back(f);
        }
    } else {
        JsonArray exist = cfg["filament_exist"].as<JsonArray>();
        if (!exist.isNull() && exist.size() > 0) {
            JsonArray vendors = cfg["filament_vendor"].as<JsonArray>();
            JsonArray types = cfg["filament_type"].as<JsonArray>();
            JsonArray subTypes = cfg["filament_sub_type"].as<JsonArray>();
            JsonArray rgba = cfg["filament_color_rgba"].as<JsonArray>();
            JsonArray mapTable = cfg["extruder_map_table"].as<JsonArray>();

            const size_t count = min(static_cast<size_t>(4), exist.size());
            for (size_t i = 0; i < count; ++i) {
                const bool loaded = exist[i] | false;
                const char *vendor = vendors[i] | "Generic";
                const char *type = types[i] | "PLA";
                const char *sub = subTypes[i] | "";
                char material[32];
                if (sub[0]) snprintf(material, sizeof(material), "%s %s %s", vendor, type, sub);
                else snprintf(material, sizeof(material), "%s %s", vendor, type);

                char color[16] = "#888888";
                const char *rawColor = rgba[i] | "";
                if (rawColor[0]) {
                    if (rawColor[0] == '#') {
                        strlcpy(color, rawColor, sizeof(color));
                    } else {
                        snprintf(color, sizeof(color), "#%.6s", rawColor);
                    }
                } else {
                    JsonArray colorMulti = cfg["filament_color_multi"].as<JsonArray>();
                    if (!colorMulti.isNull() && colorMulti.size() > i) {
                        JsonObject cm = colorMulti[i].as<JsonObject>();
                        JsonArray colors = cm["colors"].as<JsonArray>();
                        if (!colors.isNull() && colors.size() > 0) {
                            const char *hex = colors[0] | "";
                            if (hex[0]) snprintf(color, sizeof(color), "#%.6s", hex);
                        }
                    }
                }

                const int mapped = (mapTable.size() > i) ? mapTable[i].as<int>() : static_cast<int>(i);
                pushSlot(static_cast<int>(i), loaded, material, color, mapped);
            }
            Serial.printf("[Moonraker] filaments parsed: %u slots\n",
                          static_cast<unsigned>(status_.filaments.size()));
        }
    }

    JsonArray mapTable = cfg["extruder_map_table"].as<JsonArray>();
    if (!mapTable.isNull() && mapTable.size() > 0) {
        status_.activeTool = mapTable[0].as<int>();
        for (size_t i = 0; i < status_.filaments.size() && i < mapTable.size(); ++i) {
            status_.filaments[i].mappedTool = mapTable[i].as<int>();
        }
    }
}

void MoonrakerClient::parseStatusUpdate(JsonObject &status) {
    JsonObject printStats = status["print_stats"].as<JsonObject>();
    if (!printStats.isNull()) {
        const PrintState prevState = status_.printState;
        status_.printState = mapPrintState(printStats["state"]);
        if (status_.printState != PrintState::Printing && status_.printState != PrintState::Paused) {
            resetProgressCache();
        } else if (prevState != PrintState::Printing && prevState != PrintState::Paused &&
                   (status_.printState == PrintState::Printing || status_.printState == PrintState::Paused)) {
            resetProgressCache();
        }
        strlcpy(status_.filename, printStats["filename"] | "", sizeof(status_.filename));
        status_.printDuration = printStats["print_duration"] | 0.0f;
        status_.totalDuration = printStats["total_duration"] | 0.0f;
        const char *msg = printStats["message"];
        if (msg) strlcpy(status_.stateMessage, msg, sizeof(status_.stateMessage));

        JsonObject info = printStats["info"].as<JsonObject>();
        if (!info.isNull()) {
            const int totalLayer = info["total_layer"] | 0;
            const int currentLayer = info["current_layer"] | 0;
            if (totalLayer > 0 && currentLayer > 0) {
                cachedLayerProgress_ = static_cast<float>(currentLayer) / static_cast<float>(totalLayer);
            }
        }
    }

    JsonObject displayStatus = status["display_status"].as<JsonObject>();
    if (!displayStatus.isNull()) {
        float p = -1.0f;
        if (readProgressFraction(displayStatus["progress"], p)) cachedDisplayProgress_ = p;
    }

    JsonObject virtualSd = status["virtual_sdcard"].as<JsonObject>();
    if (!virtualSd.isNull()) {
        float p = -1.0f;
        if (readProgressFraction(virtualSd["progress"], p)) cachedVsdProgress_ = p;
    }

    recomputeProgress();

    parseExtruder(status, "extruder", 0);
    parseExtruder(status, "extruder1", 1);
    parseExtruder(status, "extruder2", 2);
    parseExtruder(status, "extruder3", 3);

    status_.nozzleTemp = status_.extruderTemps[status_.activeTool];
    status_.nozzleTarget = status_.extruderTargets[status_.activeTool];

    JsonObject bed = status["heater_bed"].as<JsonObject>();
    if (!bed.isNull()) {
        status_.bedTemp = bed["temperature"] | 0.0f;
        status_.bedTarget = bed["target"] | 0.0f;
    }

    JsonObject gcodeMove = status["gcode_move"].as<JsonObject>();
    if (!gcodeMove.isNull()) {
        status_.speedFactor = (gcodeMove["speed_factor"] | 1.0f) * 100.0f;
        status_.flowFactor = (gcodeMove["extrude_factor"] | 1.0f) * 100.0f;
    }

    JsonObject taskCfg = status["print_task_config"].as<JsonObject>();
    if (!taskCfg.isNull()) parsePrintTaskConfig(taskCfg);

    checkNotifications();
}

void MoonrakerClient::checkNotifications() {
    if (status_.printState == lastPrintState_) return;

    if (notifyCb_) {
        if (status_.printState == PrintState::Complete && lastPrintState_ == PrintState::Printing) {
            notifyCb_("Print Complete", status_.filename[0] ? status_.filename : "Job finished");
        } else if (status_.printState == PrintState::Error) {
            notifyCb_("Print Error", status_.stateMessage[0] ? status_.stateMessage : "Printer error");
        } else if (status_.printState == PrintState::Paused && lastPrintState_ == PrintState::Printing) {
            notifyCb_("Print Paused", status_.filename);
        }
    }
    lastPrintState_ = status_.printState;
}

void MoonrakerClient::handleMessage(const char *payload, size_t length) {
    JsonDocument doc;
    if (deserializeJson(doc, payload, length) != DeserializationError::Ok) {
        Serial.println("[Moonraker] json parse failed");
        return;
    }

    const char *method = doc["method"];
    if (method && strcmp(method, "notify_status_update") == 0) {
        JsonArray params = doc["params"].as<JsonArray>();
        if (!params.isNull() && params.size() > 0) {
            JsonObject st = params[0].as<JsonObject>();
            if (!st.isNull()) {
                parseStatusUpdate(st);
                if (statusCb_) statusCb_(status_);
            }
        }
        return;
    }

    if (doc["id"].is<int>()) {
        handleRpcResponse(doc);
    }
}

bool MoonrakerClient::pollViaRest(MoonrakerRest &rest) {
    if (status_.connection == ConnectionState::Connecting) return false;

    String response;
    if (!rest.queryPrinterStatus(response)) return false;

    JsonDocument doc;
    if (deserializeJson(doc, response) != DeserializationError::Ok) return false;

    JsonObject st = doc["result"]["status"].as<JsonObject>();
    if (st.isNull()) return false;

    parseStatusUpdate(st);
    if (status_.connection != ConnectionState::Connected) {
        restOnly_ = true;
        setConnection(ConnectionState::Connected);
        Serial.println("[Moonraker] REST fallback connected");
    }
    if (statusCb_) statusCb_(status_);
    return true;
}
