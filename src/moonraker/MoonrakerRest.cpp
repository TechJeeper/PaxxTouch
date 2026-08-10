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

namespace {

void urlEncodeAppend(const char *src, String &out) {
    for (const char *p = src; *p; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '/') {
            out += static_cast<char>(c);
        } else {
            char buf[8];
            snprintf(buf, sizeof(buf), "%%%.2X", c);
            out += buf;
        }
    }
}

void normalizeColorHex(const char *raw, char *out, size_t outLen) {
    if (!raw || !raw[0] || !out || outLen < 2) {
        if (out && outLen) out[0] = '\0';
        return;
    }
    if (raw[0] == '#') snprintf(out, outLen, "#%.6s", raw + 1);
    else snprintf(out, outLen, "#%.6s", raw);
}

int parseSemicolonColors(const char *raw, GcodePrintColor *colors, int maxColors) {
    if (!raw || !raw[0] || !colors || maxColors <= 0) return 0;

    int count = 0;
    const char *start = raw;
    for (const char *p = raw;; ++p) {
        if (*p == ';' || *p == '\0') {
            if (p > start && count < maxColors) {
                char token[20] = {};
                const size_t len = min(static_cast<size_t>(p - start), sizeof(token) - 1);
                memcpy(token, start, len);
                normalizeColorHex(token, colors[count].hex, sizeof(colors[count].hex));
                ++count;
            }
            if (*p == '\0') break;
            start = p + 1;
        }
    }
    return count;
}

}  // namespace

bool MoonrakerRest::getGcodeMetadata(const char *filename, GcodeMetadata &out) {
    out = {};
    if (!filename || !filename[0]) return false;

    String query = "/server/files/metadata?filename=";
    urlEncodeAppend(filename, query);

    String response;
    if (!requestJson("GET", query.c_str(), nullptr, response)) {
        Serial.printf("[Moonraker] metadata failed HTTP %d path=%s\n", lastStatusCode_, filename);
        return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, response) != DeserializationError::Ok) return false;

    JsonObject result = doc["result"].as<JsonObject>();
    if (result.isNull()) return false;

    strlcpy(out.filename, result["filename"] | filename, sizeof(out.filename));
    out.estimatedMinutes = (result["estimated_time"] | 0.0) / 60.0;

    const char *colourField = result["filament_colour"] | result["filament_color"] | "";
    out.colorCount = parseSemicolonColors(colourField, out.colors, 4);

    JsonArray weights = result["filament_weight"].as<JsonArray>();
    if (!weights.isNull()) {
        for (int i = 0; i < out.colorCount && i < 4; ++i) {
            out.colors[i].weightG = weights[i] | 0.0f;
        }
    }

    if (out.colorCount == 0) {
        static const char *kFallback[] = {"#FF0000", "#00FF00", "#0000FF", "#FFFF00"};
        out.colorCount = 1;
        strlcpy(out.colors[0].hex, kFallback[0], sizeof(out.colors[0].hex));
    }

    Serial.printf("[Moonraker] metadata %s colors=%d\n", out.filename, out.colorCount);
    return true;
}

bool MoonrakerRest::setExtruderMapTable(const int *toolForColor, int colorCount) {
    if (!toolForColor || colorCount <= 0) return false;

    char mapStr[96] = {};
    int pos = 0;
    for (int i = 0; i < 32; ++i) {
        const int tool = i < colorCount ? toolForColor[i] : 0;
        if (i > 0 && pos < static_cast<int>(sizeof(mapStr)) - 1) mapStr[pos++] = ',';
        pos += snprintf(mapStr + pos, sizeof(mapStr) - pos, "%d", tool);
        if (pos >= static_cast<int>(sizeof(mapStr)) - 4) break;
    }

    char script[128];
    snprintf(script, sizeof(script), "SET_EXTRUDER_MAP MAP=\"%s\"", mapStr);
    const bool ok = sendGcodeScript(script);
    Serial.printf("[Moonraker] SET_EXTRUDER_MAP %s (%s)\n", ok ? "ok" : "failed", mapStr);
    return ok;
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
