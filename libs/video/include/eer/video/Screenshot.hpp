#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

// What a screenshot records about the camera that took it. It comes from the
// config and not from the code, because it describes hardware: the same
// frontend flies cameras with different sensors and lenses.
struct CameraExif {
    std::string model;     // EXIF Model. Photogrammetry groups pictures by it
    std::string position;  // where it is mounted, in words
    std::string sensor;
    std::string lens;      // EXIF LensModel

    // Zero means the config did not say. The tags that depend on it are then
    // left out of the file, since a missing focal length makes photogrammetry
    // software estimate one where a wrong one is believed.
    double focalLengthMm = 0.0;
    double fNumber = 0.0;
    int sensorActiveWidthPx = 0;
    int sensorActiveHeightPx = 0;
    double pixelSizeUm = 0.0;

    // Multiplies the focal length written to EXIF. About 1.333 behind a flat
    // window in water, 1.0 in air.
    double refractionIndex = 1.0;

    // Anything else, appended to the EXIF user comment as key=value.
    std::vector<std::pair<std::string, std::string>> notes;
};

// The virtual electrical tape: how much of each edge a screenshot cuts off,
// as a fraction of the frame.
struct CropRect {
    bool enabled = false;
    float left = 0.0f;
    float right = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;

    // Clamps each edge and scales opposing edges back so at least 5% of the
    // frame always survives.
    void normalize();
};

struct ScreenshotRequest {
    std::filesystem::path file;

    // RGBA, top row first, exactly as streamed. Flips and the crop are applied
    // by writeScreenshot.
    std::vector<uint8_t> pixels;
    int width = 0;
    int height = 0;

    bool flipVertical = false;
    bool flipHorizontal = false;
    CropRect crop;

    int cameraNumber = 0;
    std::string label;    // "Camera 1"
    std::string section;  // "section_0"
    std::string vehicle;  // "BlueStar", may be empty
    CameraExif exif;
    std::chrono::system_clock::time_point time;
};

// Writes an uncompressed TIFF with EXIF. Slow, so run it off the render thread.
bool writeScreenshot(ScreenshotRequest request);
