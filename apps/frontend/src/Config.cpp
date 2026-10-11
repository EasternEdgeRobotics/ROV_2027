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

void readString(const json& data, const char* key, std::string& out) {
    if (data.contains(key) && data[key].is_string()) {
        out = data[key].get<std::string>();
    }
}

template <typename T>
void readNumber(const json& data, const char* key, T& out) {
    if (data.contains(key) && data[key].is_number()) {
        out = data[key].get<T>();
    }
}

void readBool(const json& data, const char* key, bool& out) {
    if (data.contains(key) && data[key].is_boolean()) {
        out = data[key].get<bool>();
    }
}

void readColour(const json& data, const char* key, ImVec4& out) {
    if (!data.contains(key) || !data[key].is_array() || data[key].size() != 4) {
        return;
    }

    float rgba[4];

    for (size_t i = 0; i < 4; ++i) {
        if (!data[key][i].is_number()) {
            return;
        }

        rgba[i] = std::clamp(data[key][i].get<float>(), 0.0f, 1.0f);
    }

    out = ImVec4(rgba[0], rgba[1], rgba[2], rgba[3]);
}

json colourToJson(const ImVec4& c) {
    return json::array({c.x, c.y, c.z, c.w});
}

void readExif(const json& data, CameraExif& exif) {
    readString(data, "model", exif.model);
    readString(data, "position", exif.position);
    readString(data, "sensor", exif.sensor);
    readString(data, "lens", exif.lens);
    readNumber(data, "focal_length_mm", exif.focalLengthMm);
    readNumber(data, "f_number", exif.fNumber);
    readNumber(data, "pixel_size_um", exif.pixelSizeUm);
    readNumber(data, "refraction_index", exif.refractionIndex);

    if (data.contains("sensor_active_px") && data["sensor_active_px"].is_array() &&
        data["sensor_active_px"].size() == 2 &&
        data["sensor_active_px"][0].is_number() &&
        data["sensor_active_px"][1].is_number()) {
        exif.sensorActiveWidthPx = data["sensor_active_px"][0].get<int>();
        exif.sensorActiveHeightPx = data["sensor_active_px"][1].get<int>();
    }

    if (data.contains("notes") && data["notes"].is_object()) {
        for (const auto& [key, value] : data["notes"].items()) {
            if (value.is_string()) {
                exif.notes.emplace_back(key, value.get<std::string>());
            }
        }
    }
}

json exifToJson(const CameraExif& exif) {
    json notes = json::object();

    for (const auto& [key, value] : exif.notes) {
        notes[key] = value;
    }

    return json{
        {"model", exif.model},
        {"position", exif.position},
        {"sensor", exif.sensor},
        {"lens", exif.lens},
        {"focal_length_mm", exif.focalLengthMm},
        {"f_number", exif.fNumber},
        {"sensor_active_px", {exif.sensorActiveWidthPx, exif.sensorActiveHeightPx}},
        {"pixel_size_um", exif.pixelSizeUm},
        {"refraction_index", exif.refractionIndex},
        {"notes", notes},
    };
}

void readCamera(const json& data, CameraConfig& camera) {
    readText(data, "url", camera.url);
    readText(data, "video_caps", camera.videoCaps);
    readText(data, "audio_caps", camera.audioCaps);

    readBool(data, "flip_vertical", camera.flipVertical);
    readBool(data, "flip_horizontal", camera.flipHorizontal);

    readBool(data, "crop_enabled", camera.crop.enabled);
    readNumber(data, "crop_left", camera.crop.left);
    readNumber(data, "crop_right", camera.crop.right);
    readNumber(data, "crop_top", camera.crop.top);
    readNumber(data, "crop_bottom", camera.crop.bottom);
    camera.crop.normalize();

    readColour(data, "crop_shade", camera.cropStyle.shade);
    readColour(data, "crop_border", camera.cropStyle.border);

    if (data.contains("exif") && data["exif"].is_object()) {
        readExif(data["exif"], camera.exif);
    }
}

json cameraToJson(const CameraConfig& camera) {
    return json{
        {"url", camera.url},
        {"video_caps", camera.videoCaps},
        {"audio_caps", camera.audioCaps},
        {"flip_vertical", camera.flipVertical},
        {"flip_horizontal", camera.flipHorizontal},
        {"crop_enabled", camera.crop.enabled},
        {"crop_left", camera.crop.left},
        {"crop_right", camera.crop.right},
        {"crop_top", camera.crop.top},
        {"crop_bottom", camera.crop.bottom},
        {"crop_shade", colourToJson(camera.cropStyle.shade)},
        {"crop_border", colourToJson(camera.cropStyle.border)},
        {"exif", exifToJson(camera.exif)},
    };
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

    json cameras = json::array();

    for (const CameraConfig& camera : config.cameras) {
        cameras.push_back(cameraToJson(camera));
    }

    output << json{{"vehicle", config.vehicle}, {"cameras", cameras}}.dump(4)
           << std::endl;

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

    if (data.contains("cameras") && data["cameras"].is_array()) {
        const json& cameras = data["cameras"];

        for (size_t i = 0; i < kCameraCount && i < cameras.size(); ++i) {
            if (cameras[i].is_object()) {
                readCamera(cameras[i], config.cameras[i]);
            }
        }
    }

    return true;
}
