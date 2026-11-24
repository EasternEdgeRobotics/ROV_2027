#pragma once

#include <SDL3/SDL.h>
#include "imgui.h"

#include <eer/video/CameraStream.hpp>
#include <eer/video/Screenshot.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <vector>

// How the crop overlay is painted over the live view. Appearance only, it
// never changes what a screenshot contains.
//
// The shade's alpha is the useful part. A task crop wants it opaque, so what
// is thrown away looks thrown away. A mounting crop wants it translucent,
// because the pilot still needs to see past it.
struct CropOverlayStyle {
    ImVec4 shade{0.0f, 0.0f, 0.0f, 0.35f};
    ImVec4 border{1.0f, 0.0f, 0.0f, 1.0f};
};

// One WHEP video stream: connects, reconnects, converts NV12 to RGBA on the
// GPU, draws itself, and takes screenshots.
//
// Per frame: prepare() before any ImGui drawing, then render().
class Camera {
public:
    enum class Status {
        Idle,         // no URL configured
        InvalidUrl,   // a URL is set, but not one that can be dialled
        Connecting,   // session started, no frame yet
        Live,         // frames arriving
        Reconnecting  // waiting out the backoff before the next attempt
    };

    // `fallback` is drawn while there is no picture.
    Camera(int cameraNumber, ImTextureID fallback);
    ~Camera();

    void start();
    void stop();

    void prepare(SDL_GPUCommandBuffer* cmd);
    void render(const ImVec2& size);

    // Connecting is deferred to the next prepare(), so the caller decides when
    // a URL is worth dialling.
    void setEndpoint(
        const std::string& url,
        const std::string& videoCaps,
        const std::string& audioCaps);

    Status status() const { return status_; }
    std::string statusText() const;
    const std::string& endpointUrl() const { return desiredUrl; }
    const std::string& endpointVideoCaps() const { return desiredVideoCaps; }
    const std::string& endpointAudioCaps() const { return desiredAudioCaps; }

    // Zero until the first frame arrives.
    ImVec2 frameSize() const { return ImVec2(float(frameWidth), float(frameHeight)); }

    // Queues a screenshot of the next frame. False if the directory cannot be
    // created.
    bool screenshot();
    void waitForScreenshotWrites();

    // True from screenshot() until the frame is read back. The picture on
    // screen when it turns false is the one in the file.
    bool screenshotPending() const { return takeScreenshot; }

    // The file the last screenshot is being written to. It may still be
    // writing, call waitForScreenshotWrites() before opening it.
    const std::filesystem::path& lastScreenshotFile() const { return lastScreenshot; }

    // While true the picture on screen stays as it is. The stream keeps
    // running, so letting go shows live video again straight away.
    bool hold = false;

    // Screenshot settings, read when a screenshot is taken.
    bool flipVertical = false;
    bool flipHorizontal = false;
    CropRect crop;
    CropOverlayStyle cropStyle;
    CameraExif exif;
    std::string vehicleName;                 // "BlueStar"
    std::string section = "section_0";       // goes in the file name
    std::filesystem::path screenshotDir;

private:
    void syncStream();
    void releaseStream();
    void drainTeardowns(bool wait);
    bool saveScreenshot();
    void drainScreenshotWrites(bool wait);
    void drawCropOverlay(const ImVec2& imageMin, const ImVec2& imageMax) const;
    void ensureTargets(int width, int height);
    void destroyGpuResources();
    bool readbackRgba(std::vector<uint8_t>& outPixels);

    SDL_GPUTexture* texY_ = nullptr;
    SDL_GPUTexture* texUV_ = nullptr;
    SDL_GPUTexture* rgbaTex_ = nullptr;
    SDL_GPUTransferBuffer* uploadBuf_ = nullptr;

    ImTextureID fallback;
    int cameraNumber = 0;
    std::string label;

    std::unique_ptr<CameraStream> stream;

    // Kept across frames on purpose. poll_frame() fills whatever buffers it
    // is given, and fresh ones every frame meant resizing two megabytes from
    // zero each time, about 15 ms per camera in an unoptimised build.
    FrameData frame_;

    // What the user asked for...
    std::string desiredUrl;
    std::string desiredVideoCaps;
    std::string desiredAudioCaps;
    bool desiredUrlValid = false;

    // ...and what the current connection attempt (or retry series) is for.
    std::string appliedUrl;
    std::string appliedVideoCaps;
    std::string appliedAudioCaps;

    bool running = false;
    Status status_ = Status::Idle;

    // Codecs the live session negotiated, for the log line and the UI.
    std::string activeCodec;

    bool takeScreenshot = false;
    std::filesystem::path lastScreenshot;

    int frameWidth = 0;
    int frameHeight = 0;

    bool hasReceivedFrame = false;
    int reconnectAttempts = 0;
    std::chrono::steady_clock::time_point lastFrameTime;
    std::chrono::steady_clock::time_point streamStartTime;
    std::chrono::steady_clock::time_point nextConnectAttempt;
    std::chrono::steady_clock::time_point lastKeyframeRequest;

    // Tearing a dead pipeline down can block (state changes, the WHEP DELETE),
    // so it happens off the render thread.
    std::vector<std::future<void>> streamTeardowns;
    std::vector<std::future<bool>> screenshotWrites;
};
