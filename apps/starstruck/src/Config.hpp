#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Text fields are char arrays because that is what ImGui::InputText edits.
struct Config {
    // Written into screenshot metadata.
    char vehicle[64] = "BlueStar";

    char url[512] = "";
    char videoCaps[1024] = "";
    char audioCaps[1024] = "";
    bool flipVertical = false;
    bool flipHorizontal = false;

    int countdownSeconds = 3;

    // Light levels, 0 to 255. Blink is the level for the on half of each
    // countdown second.
    int idleBrightness = 0;
    int blinkBrightness = 64;
    int flashBrightness = 255;

    // After this long untouched on the live view, the lights breathe between
    // idle and this level, so people walking past notice the booth. A peak at
    // or below idle turns it off.
    int breatheBrightness = 40;
    int breatheAfterSeconds = 60;

    // How long the lights are at full before the frame is taken. Covers the
    // trip to the ROV and the camera's own latency, otherwise the photo shows
    // the lights still off.
    int flashLeadMs = 500;

    // Program and arguments, started once and fed a brightness per line on
    // stdin. Empty means no lights. Not editable in the app.
    std::vector<std::string> lightsCommand;

    // Where Print puts a photo for the print script. Empty means
    // Pictures/starstruck/print.
    char printDir[512] = "";
};

// A missing file is created with the defaults. A missing or malformed key
// keeps its default. Returns false if the file could not be read.
bool loadConfig(const std::filesystem::path& path, Config& config);

bool saveConfig(const std::filesystem::path& path, const Config& config);
