#include "Config.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

// Each reader leaves `out` alone when the key is missing or the wrong type.

template <size_t N>
void readText(const json& data, const char* key, char (&out)[N]) {
    if (data.contains(key) && data[key].is_string()) {
        std::snprintf(out, N, "%s", data[key].get<std::string>().c_str());
    }
}

void readInt(const json& data, const char* key, int& out, int min, int max) {
    if (data.contains(key) && data[key].is_number()) {
        out = std::clamp(data[key].get<int>(), min, max);
    }
}

void readBool(const json& data, const char* key, bool& out) {
    if (data.contains(key) && data[key].is_boolean()) {
        out = data[key].get<bool>();
    }
}

void readStrings(const json& data, const char* key, std::vector<std::string>& out) {
    if (!data.contains(key) || !data[key].is_array()) {
        return;
    }

    std::vector<std::string> strings;

    for (const json& item : data[key]) {
        if (!item.is_string()) {
            return;
        }

        strings.push_back(item.get<std::string>());
    }

    out = strings;
}

}  // namespace

bool saveConfig(const fs::path& path, const Config& config) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    std::ofstream output(path);

    if (!output) {
        std::cerr << "Failed to open config file for writing: " << path
                  << std::endl;
        return false;
    }

    const json data{
        {"vehicle", config.vehicle},
        {"camera", {
            {"url", config.url},
            {"video_caps", config.videoCaps},
            {"audio_caps", config.audioCaps},
            {"flip_vertical", config.flipVertical},
            {"flip_horizontal", config.flipHorizontal},
        }},
        {"countdown_seconds", config.countdownSeconds},
        {"idle_brightness", config.idleBrightness},
        {"blink_brightness", config.blinkBrightness},
        {"flash_brightness", config.flashBrightness},
        {"flash_lead_ms", config.flashLeadMs},
        {"lights_command", config.lightsCommand},
        {"print_dir", config.printDir},
    };

    output << data.dump(4) << std::endl;
    return output.good();
}

bool loadConfig(const fs::path& path, Config& config) {
    if (!fs::exists(path)) {
        saveConfig(path, config);
        return false;
    }

    std::ifstream input(path);

    // No exceptions, a parse error comes back as a discarded value.
    const json data = json::parse(input, nullptr, false);

    if (!data.is_object()) {
        std::cerr << "Config file is not valid JSON: " << path << std::endl;
        return false;
    }

    readText(data, "vehicle", config.vehicle);

    if (data.contains("camera") && data["camera"].is_object()) {
        const json& camera = data["camera"];
        readText(camera, "url", config.url);
        readText(camera, "video_caps", config.videoCaps);
        readText(camera, "audio_caps", config.audioCaps);
        readBool(camera, "flip_vertical", config.flipVertical);
        readBool(camera, "flip_horizontal", config.flipHorizontal);
    }

    readInt(data, "countdown_seconds", config.countdownSeconds, 1, 10);
    readInt(data, "idle_brightness", config.idleBrightness, 0, 255);
    readInt(data, "blink_brightness", config.blinkBrightness, 0, 255);
    readInt(data, "flash_brightness", config.flashBrightness, 0, 255);
    readInt(data, "flash_lead_ms", config.flashLeadMs, 0, 5000);
    readStrings(data, "lights_command", config.lightsCommand);
    readText(data, "print_dir", config.printDir);

    return true;
}
