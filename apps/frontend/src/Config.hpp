#pragma once

#include <eer/video/Camera.hpp>

#include <filesystem>

constexpr int kCameraCount = 4;

// Text fields are char arrays because that is what ImGui::InputText edits.
struct CameraConfig {
    char url[512] = "";
    char videoCaps[1024] = "";
    char audioCaps[1024] = "";

    bool flipVertical = false;
    bool flipHorizontal = false;
    CropRect crop;
    CropOverlayStyle cropStyle;

    // Not editable in the app, only in the file. See docs/Cameras.md.
    CameraExif exif;
};

struct Config {
    // Written into screenshot metadata.
    char vehicle[64] = "BlueStar";

    CameraConfig cameras[kCameraCount];
};

// A missing file is created with the defaults. A missing or malformed key
// keeps its default and never fails the load, so one bad value cannot cost
// someone their camera URLs. Returns false if the file could not be read.
bool loadConfig(const std::filesystem::path& path, Config& config);

bool saveConfig(const std::filesystem::path& path, const Config& config);
