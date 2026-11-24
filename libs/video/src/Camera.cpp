#include <eer/video/Camera.hpp>

#include <eer/video/CameraStreamFactory.hpp>
#include <eer/video/GpuShared.hpp>

#include <gst/gst.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>

namespace fs = std::filesystem;

namespace {

// How long a fresh session gets to deliver its first frame, and how long a
// running one may stall before the session is considered dead.
constexpr auto kInitialFrameTimeout = std::chrono::seconds(8);
constexpr auto kFrameTimeout = std::chrono::seconds(3);

// Once the transport is up, a missing first frame means we are waiting on the
// sender's next keyframe, not on a broken link. A source with a long GOP can
// take several seconds, and reconnecting restarts that wait rather than
// shortening it, so the session gets considerably longer before being dropped.
constexpr auto kConnectedFirstFrameTimeout = std::chrono::seconds(25);

// A short stall is usually lost packets, not a dead link. Ask the sender for a
// keyframe first; only escalate to a full reconnect if that does not help.
constexpr auto kKeyframeStallTimeout = std::chrono::milliseconds(900);
constexpr auto kKeyframeRequestInterval = std::chrono::milliseconds(750);

// A negotiated session that has not produced a frame yet is usually waiting for
// the sender's next IDR; ask for one instead of sitting through the wait.
constexpr auto kFirstFrameKeyframeDelay = std::chrono::milliseconds(1500);

// Reconnect backoff. A camera that is switched off should not be dialled four
// times a second for the rest of the dive.
constexpr auto kReconnectBackoffMin = std::chrono::milliseconds(1000);
constexpr auto kReconnectBackoffMax = std::chrono::seconds(15);

// How long the last decoded frame stays on screen before the no-signal image
// takes over.
constexpr auto kStaleFrameTimeout = std::chrono::milliseconds(3000);

std::string trimWhitespace(const std::string& value) {
    const size_t first = value.find_first_not_of(" \t\r\n");

    if (first == std::string::npos) {
        return {};
    }

    const size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

// The WHEP backends speak HTTP; anything else is a typo, and dialling it just
// produces a stream of curl errors. Catching it here lets the UI say so.
bool isSupportedStreamUrl(const std::string& url) {
    static constexpr const char* kSchemes[] = {"http://", "https://"};

    for (const char* scheme : kSchemes) {
        const size_t schemeLen = std::strlen(scheme);

        if (url.size() <= schemeLen || url.compare(0, schemeLen, scheme) != 0) {
            continue;
        }

        const std::string rest = url.substr(schemeLen);
        const size_t hostEnd = rest.find_first_of("/?#");
        const std::string host = rest.substr(0, hostEnd);

        return !host.empty() &&
               host.find_first_of(" \t") == std::string::npos;
    }

    return false;
}

std::chrono::milliseconds reconnectDelay(int attempts) {
    auto delay = kReconnectBackoffMin;

    for (int i = 1; i < attempts && delay < kReconnectBackoffMax; ++i) {
        delay *= 2;
    }

    return std::min<std::chrono::milliseconds>(delay, kReconnectBackoffMax);
}

std::mutex gGstMutex;
int gGstUsers = 0;
GMainContext* gGstContext = nullptr;
GMainLoop* gMainLoop = nullptr;
std::thread gMainLoopThread;

void acquireGst() {
    std::lock_guard<std::mutex> lock(gGstMutex);

    if (gGstUsers++ != 0) {
        return;
    }

    gst_init(nullptr, nullptr);

    gGstContext = g_main_context_new();
    gMainLoop = g_main_loop_new(gGstContext, FALSE);

    gMainLoopThread = std::thread([] {
        g_main_context_push_thread_default(gGstContext);
        g_main_loop_run(gMainLoop);
        g_main_context_pop_thread_default(gGstContext);
    });
}

void releaseGst() {
    std::lock_guard<std::mutex> lock(gGstMutex);

    if (--gGstUsers != 0) {
        return;
    }

    if (gMainLoop) {
        g_main_loop_quit(gMainLoop);
    }

    if (gMainLoopThread.joinable()) {
        gMainLoopThread.join();
    }

    if (gMainLoop) {
        g_main_loop_unref(gMainLoop);
        gMainLoop = nullptr;
    }

    if (gGstContext) {
        g_main_context_unref(gGstContext);
        gGstContext = nullptr;
    }
}


} // namespace

template <typename F>
auto runOnGstThread(F&& fn) -> decltype(fn()) {
    using R = decltype(fn());

    auto task =
        std::make_shared<std::packaged_task<R()>>(std::forward<F>(fn));
    auto future = task->get_future();

    g_main_context_invoke_full(
        gGstContext,
        G_PRIORITY_DEFAULT,
        [](gpointer data) -> gboolean {
            auto* taskPtr =
                static_cast<std::shared_ptr<std::packaged_task<R()>>*>(data);
            (**taskPtr)();
            delete taskPtr;
            return G_SOURCE_REMOVE;
        },
        new std::shared_ptr<std::packaged_task<R()>>(task),
        nullptr);

    return future.get();
}

Camera::Camera(int cameraNumber, ImTextureID fallback)
    : fallback(fallback),
      cameraNumber(cameraNumber),
      label("Camera " + std::to_string(cameraNumber)),
      lastFrameTime(std::chrono::steady_clock::now()),
      streamStartTime(lastFrameTime),
      nextConnectAttempt(lastFrameTime),
      lastKeyframeRequest(lastFrameTime) {}

Camera::~Camera() {
    stop();

    drainScreenshotWrites(true);
    destroyGpuResources();
}

void Camera::setEndpoint(
    const std::string& url,
    const std::string& videoCaps,
    const std::string& audioCaps) {
    desiredUrl = trimWhitespace(url);
    desiredVideoCaps = trimWhitespace(videoCaps);
    desiredAudioCaps = trimWhitespace(audioCaps);
    desiredUrlValid = isSupportedStreamUrl(desiredUrl);

    if (!desiredUrl.empty() && !desiredUrlValid) {
        std::cerr << "[" << label << "] Ignoring unusable stream URL: "
                  << desiredUrl
                  << " (expected http://host:port/path or https://...)"
                  << std::endl;
    }
}

void Camera::start() {
    if (running) return;

    acquireGst();

    const auto now = std::chrono::steady_clock::now();
    hasReceivedFrame = false;
    reconnectAttempts = 0;
    lastFrameTime = now;
    streamStartTime = now;
    nextConnectAttempt = now;
    lastKeyframeRequest = now;

    appliedUrl.clear();
    appliedVideoCaps.clear();
    appliedAudioCaps.clear();

    running = true;
    syncStream();
}

void Camera::stop() {
    if (!running) return;

    releaseStream();

    // The teardowns touch GStreamer, so they have to finish before the shared
    // GStreamer main loop is torn down under them.
    drainTeardowns(true);

    appliedUrl.clear();
    appliedVideoCaps.clear();
    appliedAudioCaps.clear();

    running = false;
    hasReceivedFrame = false;
    status_ = Status::Idle;

    destroyGpuResources();
    releaseGst();
}

// Hands the stream off to a worker for shutdown. Stopping a WebRTC session can
// block on a pipeline state change and on the WHEP DELETE, neither of which
// belongs on the render thread.
void Camera::releaseStream() {
    if (!stream) {
        return;
    }

    streamTeardowns.emplace_back(std::async(
        std::launch::async,
        [old = std::move(stream)]() mutable {
            old->stop();
            old.reset();
        }));

    hasReceivedFrame = false;
    activeCodec.clear();
}

void Camera::drainTeardowns(bool wait) {
    for (auto it = streamTeardowns.begin(); it != streamTeardowns.end();) {
        const bool ready =
            it->wait_for(std::chrono::milliseconds(0)) ==
            std::future_status::ready;

        if (!wait && !ready) {
            ++it;
            continue;
        }

        try {
            it->get();
        } catch (const std::exception& e) {
            std::cerr << "[" << label << "] Stream teardown failed: "
                      << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[" << label
                      << "] Stream teardown failed with unknown exception"
                      << std::endl;
        }

        it = streamTeardowns.erase(it);
    }
}

void Camera::syncStream() {
    if (!running) return;

    drainTeardowns(false);

    const auto now = std::chrono::steady_clock::now();

    // A committed endpoint change always restarts from a clean slate: drop the
    // old session, forget the backoff, and dial immediately.
    if (desiredUrl != appliedUrl ||
        desiredVideoCaps != appliedVideoCaps ||
        desiredAudioCaps != appliedAudioCaps) {
        releaseStream();

        appliedUrl = desiredUrl;
        appliedVideoCaps = desiredVideoCaps;
        appliedAudioCaps = desiredAudioCaps;

        reconnectAttempts = 0;
        nextConnectAttempt = now;
    }

    if (appliedUrl.empty()) {
        status_ = Status::Idle;
        return;
    }

    if (!desiredUrlValid) {
        status_ = Status::InvalidUrl;
        return;
    }

    if (stream) {
        const bool streamFailed = stream->failed();

        // A connected transport that has not produced a frame is waiting for a
        // keyframe; an unconnected one is not coming back on its own.
        const auto firstFrameTimeout = stream->connected()
                                           ? kConnectedFirstFrameTimeout
                                           : kInitialFrameTimeout;

        const bool frameTimedOut =
            hasReceivedFrame
                ? (now - lastFrameTime) > kFrameTimeout
                : (now - streamStartTime) > firstFrameTimeout;

        if (!streamFailed && !frameTimedOut) {
            // Nudge the sender for a keyframe rather than throwing the session
            // away: a brief stall is usually lost packets, and a session that
            // has negotiated but shown nothing is usually waiting on an IDR.
            const bool stalled =
                hasReceivedFrame
                    ? (now - lastFrameTime) > kKeyframeStallTimeout
                    : (now - streamStartTime) > kFirstFrameKeyframeDelay;

            if (stalled &&
                (now - lastKeyframeRequest) > kKeyframeRequestInterval) {
                lastKeyframeRequest = now;
                stream->request_keyframe();
            }

            status_ = hasReceivedFrame ? Status::Live : Status::Connecting;
            return;
        }

        std::cerr << "[" << label << "] Dropping WebRTC stream: " << appliedUrl
                  << (streamFailed ? " (stream failed)" : " (frame timeout)")
                  << std::endl;

        releaseStream();

        ++reconnectAttempts;
        nextConnectAttempt = now + reconnectDelay(reconnectAttempts);
    }

    if (now < nextConnectAttempt) {
        status_ = Status::Reconnecting;
        return;
    }

    auto next = createCameraStream();

    StreamConfig cfg;
    cfg.url = appliedUrl;
    cfg.video_caps = appliedVideoCaps;
    cfg.audio_caps = appliedAudioCaps;
    cfg.label = label;

    // Start on the GStreamer thread so every element is created against the
    // same thread-default main context. The backend keeps signalling off this
    // call, so it returns without waiting on the network.
    const bool ok = runOnGstThread([&]() {
        return next->start(cfg);
    });

    if (!ok) {
        ++reconnectAttempts;
        nextConnectAttempt = now + reconnectDelay(reconnectAttempts);
        status_ = Status::Reconnecting;

        std::cerr << "[" << label
                  << "] Failed to start WebRTC stream: " << appliedUrl
                  << " (retry in "
                  << reconnectDelay(reconnectAttempts).count() << "ms)"
                  << std::endl;

        // next->start() failed, but it may still own a partially built
        // pipeline; tear it down off-thread like any other dead stream.
        stream = std::move(next);
        releaseStream();
        return;
    }

    stream = std::move(next);
    hasReceivedFrame = false;
    streamStartTime = now;
    lastFrameTime = now;
    lastKeyframeRequest = now;
    status_ = Status::Connecting;
}

std::string Camera::statusText() const {
    switch (status_) {
        case Status::Idle:
            return "No URL";

        case Status::InvalidUrl:
            return "Invalid URL";

        case Status::Connecting:
            // Separating these two tells you whether to go looking at the
            // network or at the camera's encoder.
            return (stream && stream->connected())
                       ? "Connected, waiting for video..."
                       : "Connecting...";

        case Status::Live:
            return activeCodec.empty() ? "Live" : "Live (" + activeCodec + ")";

        case Status::Reconnecting: {
            const auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    nextConnectAttempt - std::chrono::steady_clock::now());

            const long long seconds =
                std::max<long long>(0, (remaining.count() + 999) / 1000);

            return "Retrying in " + std::to_string(seconds) + "s (attempt " +
                   std::to_string(reconnectAttempts) + ")";
        }
    }

    return "Unknown";
}

void Camera::render(const ImVec2& size) {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = now - lastFrameTime;

    if (rgbaTex_ != nullptr && hasReceivedFrame && elapsed <= kStaleFrameTimeout) {
        float u0 = flipHorizontal ? 1.0f : 0.0f;
        float u1 = flipHorizontal ? 0.0f : 1.0f;
        float v0 = flipVertical ? 0.0f : 1.0f;
        float v1 = flipVertical ? 1.0f : 0.0f;

        ImGui::Image(
            (ImTextureID)(intptr_t)rgbaTex_,
            size,
            ImVec2(u0, v0),
            ImVec2(u1, v1));

        drawCropOverlay(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    } else {
        ImGui::Image(fallback, size);
    }
}

bool Camera::screenshot() {
    std::error_code ec;
    fs::create_directories(screenshotDir, ec);

    if (ec || screenshotDir.empty()) {
        std::cerr << "[" << label << "] Cannot use screenshot directory: "
                  << screenshotDir << std::endl;
        return false;
    }

    // Taken in prepare(), straight after the next frame is converted.
    takeScreenshot = true;
    return true;
}

bool Camera::saveScreenshot() {
    if (rgbaTex_ == nullptr || frameWidth <= 0 || frameHeight <= 0) {
        return false;
    }

    ScreenshotRequest request;
    request.time = std::chrono::system_clock::now();

    if (!readbackRgba(request.pixels)) {
        std::cerr << "[" << label << "] Screenshot readback failed" << std::endl;
        return false;
    }

    const auto timestamp =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            request.time.time_since_epoch())
            .count();

    request.file = screenshotDir /
        ("captures_" + std::to_string(timestamp) + "_" + section +
         "_Camera_" + std::to_string(cameraNumber) + ".tiff");

    lastScreenshot = request.file;
    request.width = frameWidth;
    request.height = frameHeight;
    request.flipVertical = flipVertical;
    request.flipHorizontal = flipHorizontal;
    request.crop = crop;
    request.cameraNumber = cameraNumber;
    request.label = label;
    request.section = section;
    request.vehicle = vehicleName;
    request.exif = exif;

    // Flipping, cropping and the TIFF write all happen on the worker.
    screenshotWrites.emplace_back(std::async(
        std::launch::async,
        [request = std::move(request)]() mutable {
            return writeScreenshot(std::move(request));
        }));

    return true;
}

void Camera::drainScreenshotWrites(bool wait) {
    for (auto it = screenshotWrites.begin(); it != screenshotWrites.end();) {
        const bool ready =
            it->wait_for(std::chrono::milliseconds(0)) ==
            std::future_status::ready;

        if (!wait && !ready) {
            ++it;
            continue;
        }

        if (!it->get()) {
            std::cerr << "[" << label << "] Failed to save screenshot"
                      << std::endl;
        }

        it = screenshotWrites.erase(it);
    }
}

void Camera::waitForScreenshotWrites() {
    drainScreenshotWrites(true);
}

void Camera::drawCropOverlay(
    const ImVec2& imageMin,
    const ImVec2& imageMax) const
{
    if (!crop.enabled) {
        return;
    }

    const float imageWidth = imageMax.x - imageMin.x;
    const float imageHeight = imageMax.y - imageMin.y;

    if (imageWidth <= 0.0f || imageHeight <= 0.0f) {
        return;
    }

    const ImVec2 cropMin(
        imageMin.x + imageWidth * crop.left,
        imageMin.y + imageHeight * crop.top);

    const ImVec2 cropMax(
        imageMax.x - imageWidth * crop.right,
        imageMax.y - imageHeight * crop.bottom);

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const ImU32 borderColor = ImGui::ColorConvertFloat4ToU32(cropStyle.border);
    const ImU32 shadeColor = ImGui::ColorConvertFloat4ToU32(cropStyle.shade);

    // The dark outline behind the border follows the border's alpha, so
    // fading the border out does not leave a black rectangle behind.
    const ImU32 shadowColor = IM_COL32(
        0, 0, 0,
        static_cast<int>(180.0f * std::clamp(cropStyle.border.w, 0.0f, 1.0f)));

    // Shade cropped-out regions.
    drawList->AddRectFilled(
        imageMin,
        ImVec2(imageMax.x, cropMin.y),
        shadeColor);

    drawList->AddRectFilled(
        ImVec2(imageMin.x, cropMax.y),
        imageMax,
        shadeColor);

    drawList->AddRectFilled(
        ImVec2(imageMin.x, cropMin.y),
        ImVec2(cropMin.x, cropMax.y),
        shadeColor);

    drawList->AddRectFilled(
        ImVec2(cropMax.x, cropMin.y),
        ImVec2(imageMax.x, cropMax.y),
        shadeColor);

    drawList->AddRect(
        ImVec2(cropMin.x - 1.0f, cropMin.y - 1.0f),
        ImVec2(cropMax.x + 1.0f, cropMax.y + 1.0f),
        shadowColor,
        0.0f,
        0,
        3.0f);

    drawList->AddRect(
        cropMin,
        cropMax,
        borderColor,
        0.0f,
        0,
        2.0f);
}


void Camera::ensureTargets(int width, int height) {
    if (rgbaTex_ && frameWidth == width && frameHeight == height) {
        return;
    }

    destroyGpuResources();

    SDL_GPUDevice* device = gpuShared().device;

    SDL_GPUTextureCreateInfo info = {};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;

    info.format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = width;
    info.height = height;
    texY_ = SDL_CreateGPUTexture(device, &info);

    info.format = SDL_GPU_TEXTUREFORMAT_R8G8_UNORM;
    info.width = width / 2;
    info.height = height / 2;
    texUV_ = SDL_CreateGPUTexture(device, &info);

    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage =
        SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    info.width = width;
    info.height = height;
    rgbaTex_ = SDL_CreateGPUTexture(device, &info);

    // Y plane + UV plane (w * h + w * h/2).
    const size_t needed =
        static_cast<size_t>(width) * height +
        static_cast<size_t>(width) * (height / 2);

    SDL_GPUTransferBufferCreateInfo tbInfo = {};
    tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbInfo.size = static_cast<Uint32>(needed);
    uploadBuf_ = SDL_CreateGPUTransferBuffer(device, &tbInfo);

    if (!texY_ || !texUV_ || !rgbaTex_ || !uploadBuf_) {
        std::cerr << "[" << label << "] Failed to create GPU resources: "
                  << SDL_GetError() << std::endl;
    }

    frameWidth = width;
    frameHeight = height;
}

void Camera::destroyGpuResources() {
    SDL_GPUDevice* device = gpuShared().device;
    if (!device) return;

    if (uploadBuf_) {
        SDL_ReleaseGPUTransferBuffer(device, uploadBuf_);
        uploadBuf_ = nullptr;
    }
    if (texY_)   { SDL_ReleaseGPUTexture(device, texY_);   texY_ = nullptr; }
    if (texUV_)  { SDL_ReleaseGPUTexture(device, texUV_);  texUV_ = nullptr; }
    if (rgbaTex_) {
        SDL_ReleaseGPUTexture(device, rgbaTex_);
        rgbaTex_ = nullptr;
    }
}

void Camera::prepare(SDL_GPUCommandBuffer* cmd) {
    drainScreenshotWrites(false);
    syncStream();

    if (!stream) return;

    if (!stream->poll_frame(frame_)) {
        return;
    }

    if (frame_.width <= 0 || frame_.height <= 0) {
        return;
    }

    // Still polled while held, or the frames would pile up in the stream and
    // the status would go stale.
    if (hold && hasReceivedFrame) {
        lastFrameTime = std::chrono::steady_clock::now();
        return;
    }

    ensureTargets(frame_.width, frame_.height);

    if (!uploadBuf_ || !rgbaTex_) {
        return;
    }

    SDL_GPUDevice* device = gpuShared().device;

    // --- Upload NV12 planes ---
    const size_t ySize = static_cast<size_t>(frame_.width) * frame_.height;
    const size_t uvSize =
        static_cast<size_t>(frame_.width) * (frame_.height / 2);

    uint8_t* mapped = static_cast<uint8_t*>(
        SDL_MapGPUTransferBuffer(device, uploadBuf_, true));

    if (!mapped) {
        std::cerr << "[" << label << "] Failed to map transfer buffer: "
                  << SDL_GetError() << std::endl;
        return;
    }

    std::memcpy(mapped, frame_.y_plane.data(), ySize);
    std::memcpy(mapped + ySize, frame_.uv_plane.data(), uvSize);
    SDL_UnmapGPUTransferBuffer(device, uploadBuf_);

    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTextureTransferInfo src = {};
    SDL_GPUTextureRegion dst = {};
    dst.w = frame_.width;
    dst.h = frame_.height;
    dst.d = 1;

    src.transfer_buffer = uploadBuf_;
    src.offset = 0;
    dst.texture = texY_;
    SDL_UploadToGPUTexture(copyPass, &src, &dst, false);

    src.offset = static_cast<Uint32>(ySize);
    dst.texture = texUV_;
    dst.w = frame_.width / 2;
    dst.h = frame_.height / 2;
    SDL_UploadToGPUTexture(copyPass, &src, &dst, false);

    SDL_EndGPUCopyPass(copyPass);

    // --- NV12 -> RGBA conversion pass ---
    SDL_GPUColorTargetInfo target = {};
    target.texture = rgbaTex_;
    target.load_op = SDL_GPU_LOADOP_DONT_CARE;
    target.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);

    SDL_BindGPUGraphicsPipeline(pass, gpuShared().nv12Pipeline);

    SDL_GPUTextureSamplerBinding bindings[2] = {};
    bindings[0].texture = texY_;
    bindings[0].sampler = gpuShared().sampler;
    bindings[1].texture = texUV_;
    bindings[1].sampler = gpuShared().sampler;
    SDL_BindGPUFragmentSamplers(pass, 0, bindings, 2);

    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);

    lastFrameTime = std::chrono::steady_clock::now();

    // First frame of a session: the endpoint works, so the retry series that
    // got us here is over.
    if (!hasReceivedFrame) {
        hasReceivedFrame = true;
        reconnectAttempts = 0;

        const auto connectMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                lastFrameTime - streamStartTime)
                .count();

        activeCodec = stream->codec_summary();

        std::cout << "[" << label << "] Live: " << frame_.width << "x"
                  << frame_.height;

        if (!activeCodec.empty()) {
            std::cout << " " << activeCodec;
        }

        std::cout << ", first frame in " << connectMs << "ms" << std::endl;
    }

    status_ = Status::Live;

    if (takeScreenshot) {
        takeScreenshot = false;
        saveScreenshot();
    }
}


bool Camera::readbackRgba(std::vector<uint8_t>& outPixels) {
    SDL_GPUDevice* device = gpuShared().device;

    const size_t byteCount =
        static_cast<size_t>(frameWidth) * frameHeight * 4;

    SDL_GPUTransferBufferCreateInfo tbInfo = {};
    tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    tbInfo.size = static_cast<Uint32>(byteCount);

    SDL_GPUTransferBuffer* downloadBuf =
        SDL_CreateGPUTransferBuffer(device, &tbInfo);

    if (!downloadBuf) {
        return false;
    }

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTextureRegion region = {};
    region.texture = rgbaTex_;
    region.w = frameWidth;
    region.h = frameHeight;
    region.d = 1;

    SDL_GPUTextureTransferInfo dst = {};
    dst.transfer_buffer = downloadBuf;

    SDL_DownloadFromGPUTexture(copyPass, &region, &dst);
    SDL_EndGPUCopyPass(copyPass);

    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);

    if (!fence) {
        SDL_ReleaseGPUTransferBuffer(device, downloadBuf);
        return false;
    }

    SDL_WaitForGPUFences(device, true, &fence, 1);
    SDL_ReleaseGPUFence(device, fence);

    void* mapped = SDL_MapGPUTransferBuffer(device, downloadBuf, false);

    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(device, downloadBuf);
        return false;
    }

    // The NV12 pass leaves rgbaTex_ bottom up (render() compensates with
    // inverted V texcoords), so copy the rows in reverse to return top down.
    const size_t stride = static_cast<size_t>(frameWidth) * 4;
    const uint8_t* source = static_cast<const uint8_t*>(mapped);

    outPixels.resize(byteCount);

    for (int y = 0; y < frameHeight; ++y) {
        std::memcpy(
            outPixels.data() + static_cast<size_t>(y) * stride,
            source + static_cast<size_t>(frameHeight - 1 - y) * stride,
            stride);
    }

    SDL_UnmapGPUTransferBuffer(device, downloadBuf);
    SDL_ReleaseGPUTransferBuffer(device, downloadBuf);

    return true;
}