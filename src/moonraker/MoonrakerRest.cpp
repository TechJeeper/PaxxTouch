#include "moonraker/MoonrakerRest.h"

#include <HTTPClient.h>
#include <WiFiClient.h>

void MoonrakerRest::configure(const char *host, uint16_t port, bool useAuth, const char *user, const char *pass, const char *apiKey) {
    strlcpy(host_, host, sizeof(host_));
    port_ = port;
    useAuth_ = useAuth;
    token_[0] = '\0';
    if (user) strlcpy(user_, user, sizeof(user_));
    if (pass) strlcpy(pass_, pass, sizeof(pass_));
    if (apiKey) strlcpy(apiKey_, apiKey, sizeof(apiKey_));
    else apiKey_[0] = '\0';
}

bool MoonrakerRest::requestJson(const char *method, const char *path, const char *body, String &out) {
    if (!host_[0]) return false;

    HTTPClient http;
    WiFiClient client;
    String url = String("http://") + host_ + ":" + port_ + path;
    http.begin(client, url);
    http.setTimeout(10000);
    http.addHeader("Content-Type", "application/json");
    if (apiKey_[0] != '\0') {
        http.addHeader("X-Api-Key", apiKey_);
    } else if (token_[0] != '\0') {
        http.addHeader("Authorization", String("Bearer ") + token_);
    } else if (useAuth_) {
        http.setAuthorization(user_, pass_);
    }

    const int code = (strcmp(method, "POST") == 0) ? http.POST(body ? body : "") : http.GET();
    lastStatusCode_ = code;
    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }
    out = http.getString();
    http.end();
    return true;
}

bool MoonrakerRest::login(String &tokenOut) {
    if (apiKey_[0] != '\0') {
        strlcpy(token_, apiKey_, sizeof(token_));
        tokenOut = token_;
        return true;
    }
    if (!useAuth_) return true;

    JsonDocument body;
    body["username"] = user_;
    body["password"] = pass_;
    String payload;
    serializeJson(body, payload);

    String response;
    if (!requestJson("POST", "/access/login", payload.c_str(), response)) {
        return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, response) != DeserializationError::Ok) return false;
    const char *token = doc["result"]["token"];
    if (!token) return false;
    strlcpy(token_, token, sizeof(token_));
    tokenOut = token_;
    return true;
}

bool MoonrakerRest::pingServer() {
    String out;
    const bool ok = requestJson("GET", "/server/info", nullptr, out);
    if (!ok) {
        Serial.printf("[Moonraker] /server/info failed HTTP %d\n", lastStatusCode_);
    }
    return ok;
}

bool MoonrakerRest::queryPrinterStatus(String &statusJsonOut) {
    return requestJson("GET",
                       "/printer/objects/query?"
                       "print_stats&extruder&extruder1&extruder2&extruder3&"
                       "heater_bed&gcode_move&display_status&virtual_sdcard&print_task_config",
                       nullptr, statusJsonOut);
}

void MoonrakerRest::listFiles(const char *root, const char *path, FileListCallback cb) {
    std::vector<MoonrakerFileEntry> files;

    auto collectFromArray = [&](JsonArray arr) {
        if (arr.isNull()) return;
        for (JsonObject item : arr) {
            MoonrakerFileEntry e{};
            strlcpy(e.root, root, sizeof(e.root));
            const char *itemPath = item["path"] | item["filename"] | "";
            strlcpy(e.path, itemPath, sizeof(e.path));
            e.size = item["size"] | 0;
            e.modified = item["modified"] | 0.0;
            const char *type = item["type"] | "";
            e.isDir = (strcmp(type, "dir") == 0) || item["dirname"].is<const char *>();
            files.push_back(e);
        }
    };

    auto parseResponse = [&](const String &response) -> bool {
        JsonDocument doc;
        if (deserializeJson(doc, response) != DeserializationError::Ok) return false;

        JsonVariant result = doc["result"];
        if (result.is<JsonArray>()) {
            collectFromArray(result.as<JsonArray>());
            return true;
        }
        if (result.is<JsonObject>()) {
            JsonObject obj = result.as<JsonObject>();
            collectFromArray(obj["files"].as<JsonArray>());
            collectFromArray(obj["dirs"].as<JsonArray>());
            return !files.empty() || obj["files"].is<JsonArray>() || obj["dirs"].is<JsonArray>();
        }
        return false;
    };

    JsonDocument body;
    body["action"] = "list";
    body["root"] = root;
    body["path"] = path ? path : "";
    String payload;
    serializeJson(body, payload);

    String response;
    if (requestJson("POST", "/server/files/list", payload.c_str(), response) && parseResponse(response)) {
        cb(true, files);
        return;
    }

    files.clear();
    char getPath[96];
    snprintf(getPath, sizeof(getPath), "/server/files/list?root=%s", root);
    if (path && path[0]) {
        char suffix[64];
        snprintf(suffix, sizeof(suffix), "&path=%s", path);
        strlcat(getPath, suffix, sizeof(getPath));
    }
    if (requestJson("GET", getPath, nullptr, response) && parseResponse(response)) {
        cb(true, files);
        return;
    }

    Serial.printf("[Moonraker] listFiles failed HTTP %d root=%s\n", lastStatusCode_, root);
    cb(false, files);
}

static void collectTimelapses(const std::vector<MoonrakerFileEntry> &files, std::vector<TimelapseEntry> &items) {
    for (const auto &f : files) {
        if (f.isDir) continue;
        const char *name = strrchr(f.path, '/');
        name = name ? name + 1 : f.path;
        if (!strstr(name, ".mp4")) continue;
        TimelapseEntry t{};
        strlcpy(t.name, name, sizeof(t.name));
        strlcpy(t.path, f.path, sizeof(t.path));
        t.size = f.size;
        t.modified = f.modified;
        items.push_back(t);
    }
}

void MoonrakerRest::listTimelapses(TimelapseListCallback cb) {
    std::vector<TimelapseEntry> items;
    bool anyOk = false;

    listFiles("timelapse", "", [&](bool ok, const std::vector<MoonrakerFileEntry> &files) {
        if (ok) {
            anyOk = true;
            collectTimelapses(files, items);
        }
    });

    listFiles("gcodes", "", [&](bool ok, const std::vector<MoonrakerFileEntry> &files) {
        if (ok) {
            anyOk = true;
            for (const auto &f : files) {
                if (f.isDir) continue;
                const char *name = strrchr(f.path, '/');
                name = name ? name + 1 : f.path;
                if (strstr(name, "timelapse") && strstr(name, ".mp4")) {
                    TimelapseEntry t{};
                    strlcpy(t.name, name, sizeof(t.name));
                    strlcpy(t.path, f.path, sizeof(t.path));
                    t.size = f.size;
                    t.modified = f.modified;
                    items.push_back(t);
                }
            }
        }
    });

    cb(anyOk, items);
}

bool MoonrakerRest::startPrint(const char *root, const char *filename) {
    auto tryStart = [&](const char *name) -> bool {
        JsonDocument body;
        body["filename"] = name;
        if (root && root[0]) body["root"] = root;
        String payload;
        serializeJson(body, payload);
        String response;
        return requestJson("POST", "/printer/print/start", payload.c_str(), response);
    };

    if (tryStart(filename)) return true;

    const char *base = strrchr(filename, '/');
    base = base ? base + 1 : filename;
    if (base != filename) return tryStart(base);
    return false;
}

bool MoonrakerRest::sendGcodeScript(const char *script) {
    if (!script || !script[0]) return false;
    JsonDocument body;
    body["script"] = script;
    String payload;
    serializeJson(body, payload);
    String response;
    return requestJson("POST", "/printer/gcode/script", payload.c_str(), response);
}

bool MoonrakerRest::setFilamentSlot(int slot, const char *material, const char *color) {
    JsonDocument body;
    body["slot"] = slot;
    body["material"] = material;
    body["color"] = color;
    String payload;
    serializeJson(body, payload);
    String response;
    return requestJson("POST", "/printer/filament_detect/set", payload.c_str(), response);
}
