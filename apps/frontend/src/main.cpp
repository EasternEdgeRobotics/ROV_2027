#include "Config.hpp"

#include <eer/app/App.hpp>
#include <eer/app/Image.hpp>
#include <eer/app/Paths.hpp>
#include <eer/app/Theme.hpp>
#include <eer/app/Ui.hpp>
#include <eer/video/Camera.hpp>
#include <eer/video/CameraStreamFactory.hpp>
#include <eer/video/GpuShared.hpp>

#include <SDL3/SDL_main.h>
#include "imgui.h"

#include "build_info.h"
#include "nosignal_jpg.h"

#if defined(EER_WINDOW_ICON)
#include "window_icon.h"
#endif

#include <ctime>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Must match the ID in CMakeLists.txt.
constexpr const char* kAppId = "com.easternedgerobotics.frontend2027";
constexpr const char* kFileName = "frontend";

// When the proper input library exists, these should be replaced with that!
constexpr SDL_Scancode kScreenshotKeys[kCameraCount] = {
    SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_F};
constexpr SDL_Scancode kCropKeys[kCameraCount] = {
    SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4};
constexpr SDL_Scancode kNextSectionKey = SDL_SCANCODE_T;
constexpr SDL_Scancode kSettingsKey = SDL_SCANCODE_F1;
constexpr SDL_Scancode kThemeEditorKey = SDL_SCANCODE_F10;

using Cameras = std::vector<std::unique_ptr<Camera>>;

struct State {
    Config config;
    fs::path configPath;
    std::string configStatus;

    // Screenshots go to <sessionDir>/section_<n>.
    fs::path sessionDir;
    int section = 0;
    int photos = 0;  // taken in the current section

    bool showCameras = true;
    bool showSettings = false;
    bool showThemeEditor = false;
};

// True on the frame the key goes down. Ignored while a text field has focus,
// otherwise typing a camera URL would take screenshots. ImGui is only asked
// about focus here, the key itself comes from SDL.
bool pressed(const App& app, SDL_Scancode key) {
    return !ImGui::GetIO().WantTextInput && app.keyPressed[key];
}

std::string timestampForPath() {
    const std::time_t now = std::time(nullptr);

    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif

    char text[32];
    std::strftime(text, sizeof(text), "%Y-%m-%d_%H-%M-%S", &local);
    return text;
}

ImVec4 statusColour(Camera::Status status) {
    switch (status) {
        case Camera::Status::Live:         return ui::kGood;
        case Camera::Status::Connecting:   return ui::kInfo;
        case Camera::Status::Reconnecting: return ui::kWarn;
        case Camera::Status::InvalidUrl:   return ui::kBad;
        case Camera::Status::Idle:         break;
    }

    return ui::kIdle;
}

// Points every camera at the current section's directory.
void applySection(const State& state, Cameras& cameras) {
    const std::string name = "section_" + std::to_string(state.section);

    for (auto& camera : cameras) {
        camera->section = name;
        camera->screenshotDir = state.sessionDir / name;
    }
}

void nextSection(State& state, Cameras& cameras) {
    ++state.section;
    state.photos = 0;
    applySection(state, cameras);
}

void takeScreenshot(State& state, Cameras& cameras, int index) {
    if (cameras[index]->screenshot()) {
        ++state.photos;
    }
}

// Copies the settings a camera reads every frame. Cheap, and it means the
// settings window only ever edits the config.
void applyCameraSettings(const State& state, Cameras& cameras) {
    for (int i = 0; i < kCameraCount; ++i) {
        const CameraConfig& from = state.config.cameras[i];

        cameras[i]->flipVertical = from.flipVertical;
        cameras[i]->flipHorizontal = from.flipHorizontal;
        cameras[i]->crop = from.crop;
        cameras[i]->cropStyle = from.cropStyle;
        cameras[i]->vehicleName = state.config.vehicle;
    }
}

void drawMenuBar(State& state) {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }

    if (ImGui::BeginMenu("Windows")) {
        // The camera view has no title bar to close it from, so this toggles.
        if (ImGui::MenuItem("Cameras")) {
            state.showCameras = !state.showCameras;
        }

        if (ImGui::MenuItem("Settings", SDL_GetScancodeName(kSettingsKey))) {
            state.showSettings = !state.showSettings;
        }
        ImGui::EndMenu();
    }

    char info[96];
    std::snprintf(
        info, sizeof(info), "Section %d    Photos %d    %.0f FPS",
        state.section, state.photos, ImGui::GetIO().Framerate);

    ImGui::SameLine();
    ui::alignRight(ImGui::CalcTextSize(info).x);
    ImGui::TextUnformatted(info);

    ImGui::EndMainMenuBar();
}

// The action bar along the bottom. Returns its height.
//
// An ordinary window and not a second menu bar, because ImGui only reserves
// work area for the top one.
float drawBottomBar(State& state, Cameras& cameras) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float height = ImGui::GetFrameHeight() + 8.0f;

    ImGui::SetNextWindowPos(ImVec2(
        viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - height));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, height));

    // Same colour as the menu bar and no border, so the two bars and the
    // camera grid read as one surface.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.WindowPadding.x, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));

    ImGui::Begin(
        "##bottom_bar",
        nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Capture");
    ImGui::SameLine();

    for (int i = 0; i < kCameraCount; ++i) {
        const std::string label = "Cam " + std::to_string(i + 1);

        if (ImGui::Button(label.c_str())) {
            takeScreenshot(state, cameras, i);
        }

        ImGui::SameLine();
    }

    const char* nextLabel = "Next Section";
    ui::alignRight(ImGui::CalcTextSize(nextLabel).x + style.FramePadding.x * 2.0f);

    if (ImGui::Button(nextLabel)) {
        nextSection(state, cameras);
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    return height;
}

// A 2x2 grid filling everything between the two bars.
void drawCameraGrid(Cameras& cameras, float bottomBarHeight) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(
        viewport->WorkSize.x, viewport->WorkSize.y - bottomBarHeight));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));

    // NoMove is needed as well, a window with no title bar can otherwise be
    // dragged by its body.
    ImGui::Begin(
        "##cameras",
        nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

    const ImVec2 origin = ImGui::GetCursorPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 gap = ImGui::GetStyle().ItemSpacing;
    const ImVec2 cell((avail.x - gap.x) * 0.5f, (avail.y - gap.y) * 0.5f);

    for (int i = 0; i < kCameraCount; ++i) {
        ImGui::SetCursorPos(ImVec2(
            origin.x + (i % 2) * (cell.x + gap.x),
            origin.y + (i / 2) * (cell.y + gap.y)));

        cameras[i]->render(cell);
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
}

void drawSaveRow(State& state) {
    ImGui::Spacing();

    if (ImGui::Button("Save")) {
        state.configStatus = saveConfig(state.configPath, state.config)
                                 ? "Saved"
                                 : "Failed to save";
    }

    ImGui::SameLine();
    ImGui::TextDisabled("%s", state.configStatus.c_str());
}

void drawCamerasTab(State& state, Cameras& cameras) {
    ImGui::TextDisabled(
        "WHEP URLs, for example http://192.168.137.200:8889/cam/whep. "
        "Applied when you leave a field or press Enter.");

#if defined(EER_VIDEO_BACKEND_WEBRTCBIN)
    ImGui::TextDisabled(
        "Caps can be left blank. Set audio caps to \"none\" for a camera "
        "that sends no audio.");
#endif

    ImGui::Spacing();
    ui::fieldLabel("Vehicle");
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputText("##vehicle", state.config.vehicle, sizeof(state.config.vehicle));
    ImGui::Spacing();

    const ImGuiTableFlags flags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable;

    if (ImGui::BeginTable("cameras", 5, flags)) {
        ImGui::TableSetupColumn("Camera", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("URL", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("Video Caps", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Audio Caps", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 220.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < kCameraCount; ++i) {
            CameraConfig& config = state.config.cameras[i];
            Camera& camera = *cameras[i];

            ImGui::TableNextRow();
            ImGui::PushID(i);

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Camera %d", i + 1);

            // Only redial once a field is left, so a half typed URL never
            // starts a connection attempt.
            bool committed = false;

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##url", config.url, sizeof(config.url));
            committed |= ImGui::IsItemDeactivatedAfterEdit();

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##video", config.videoCaps, sizeof(config.videoCaps));
            committed |= ImGui::IsItemDeactivatedAfterEdit();

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##audio", config.audioCaps, sizeof(config.audioCaps));
            committed |= ImGui::IsItemDeactivatedAfterEdit();

            if (committed) {
                camera.setEndpoint(config.url, config.videoCaps, config.audioCaps);
            }

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(
                statusColour(camera.status()), "%s", camera.statusText().c_str());

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    drawSaveRow(state);
}

void drawCaptureTab(State& state) {
    ImGui::TextDisabled("Screenshots from this session are written to:");
    ImGui::TextWrapped("%s", state.sessionDir.string().c_str());
    ImGui::Spacing();

    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg;

    // VET is the virtual electrical tape: the crop applied to screenshots.
    ImGui::SeparatorText("Flip and VET crop");

    if (ImGui::BeginTable("crop", 8, flags)) {
        ImGui::TableSetupColumn("Camera", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Flip V", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Flip H", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Crop", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Left", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Right", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Top", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Bottom", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (int i = 0; i < kCameraCount; ++i) {
            CameraConfig& config = state.config.cameras[i];
            CropRect& crop = config.crop;

            ImGui::TableNextRow();
            ImGui::PushID(i);

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Camera %d", i + 1);

            ImGui::TableNextColumn();
            ImGui::Checkbox("##flipv", &config.flipVertical);
            ImGui::TableNextColumn();
            ImGui::Checkbox("##fliph", &config.flipHorizontal);
            ImGui::TableNextColumn();
            ImGui::Checkbox("##crop", &crop.enabled);

            float* edges[] = {&crop.left, &crop.right, &crop.top, &crop.bottom};

            for (int edge = 0; edge < 4; ++edge) {
                ImGui::TableNextColumn();
                ImGui::PushID(edge);
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::SliderFloat("##edge", edges[edge], 0.0f, 0.95f, "%.2f");
                ImGui::PopID();
            }

            crop.normalize();
            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("VET overlay colours");

    // Opaque suits a task crop, translucent suits a crop that is always on.
    if (ImGui::Button("All opaque")) {
        for (CameraConfig& config : state.config.cameras) {
            config.cropStyle.shade.w = 1.0f;
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("All translucent")) {
        for (CameraConfig& config : state.config.cameras) {
            config.cropStyle.shade.w = 0.35f;
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Copy camera 1 to all")) {
        for (CameraConfig& config : state.config.cameras) {
            config.cropStyle = state.config.cameras[0].cropStyle;
        }
    }

    if (ImGui::BeginTable("crop_colours", 3, flags)) {
        ImGui::TableSetupColumn("Camera", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Cropped area", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Outline", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        const ImGuiColorEditFlags colourFlags =
            ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf;

        for (int i = 0; i < kCameraCount; ++i) {
            CropOverlayStyle& style = state.config.cameras[i].cropStyle;

            ImGui::TableNextRow();
            ImGui::PushID(i);

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Camera %d", i + 1);

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::ColorEdit4("##shade", &style.shade.x, colourFlags);

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::ColorEdit4("##border", &style.border.x, colourFlags);

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    drawSaveRow(state);
}

void drawControlsTab() {
    if (!ui::beginInfoTable("controls")) {
        return;
    }

    for (int i = 0; i < kCameraCount; ++i) {
        const std::string text = "Capture camera " + std::to_string(i + 1);
        ui::infoRow(SDL_GetScancodeName(kScreenshotKeys[i]), text.c_str());
    }

    for (int i = 0; i < kCameraCount; ++i) {
        const std::string text = "Toggle crop on camera " + std::to_string(i + 1);
        ui::infoRow(SDL_GetScancodeName(kCropKeys[i]), text.c_str());
    }

    ui::infoRow(SDL_GetScancodeName(kNextSectionKey), "Next section");
    ui::infoRow(SDL_GetScancodeName(kSettingsKey), "Settings");
    ui::infoRow(SDL_GetScancodeName(kThemeEditorKey), "Theme editor");

    ui::endInfoTable();
}

void drawAboutTab(const State& state, SDL_GPUDevice* gpu) {
    ImGui::TextUnformatted("Eastern Edge Robotics Frontend");
    ImGui::TextDisabled("MATE ROV 2027");

    ImGui::Spacing();

    if (ui::beginInfoTable("build")) {
        const SDL_PropertiesID props = SDL_GetGPUDeviceProperties(gpu);
        const char* driver = SDL_GetGPUDeviceDriver(gpu);

        const std::string gpuDriver =
            std::string(SDL_GetStringProperty(
                props, SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING, "unknown")) +
            " " +
            SDL_GetStringProperty(
                props, SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING, "");

        ui::infoRow("Built", EER_BUILD_DATE);
        ui::infoRow("Camera backend", cameraStreamBackendName());
        ui::infoRow("Renderer", driver ? driver : "unknown");
        ui::infoRow("GPU", SDL_GetStringProperty(
            props, SDL_PROP_GPU_DEVICE_NAME_STRING, "unknown"));
        ui::infoRow("GPU driver", gpuDriver.c_str());
        ui::endInfoTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Config: %s", state.configPath.string().c_str());
}

void drawSettings(State& state, Cameras& cameras, SDL_GPUDevice* gpu) {
    ImGui::SetNextWindowSize(ImVec2(900, 520), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Settings", &state.showSettings) &&
        ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("Cameras")) {
            drawCamerasTab(state, cameras);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Capture")) {
            drawCaptureTab(state);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Controls")) {
            drawControlsTab();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("About")) {
            drawAboutTab(state, gpu);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

void handleShortcuts(const App& app, State& state, Cameras& cameras) {
    for (int i = 0; i < kCameraCount; ++i) {
        if (pressed(app, kScreenshotKeys[i])) {
            takeScreenshot(state, cameras, i);
        }

        if (pressed(app, kCropKeys[i])) {
            CropRect& crop = state.config.cameras[i].crop;
            crop.enabled = !crop.enabled;
        }
    }

    if (pressed(app, kNextSectionKey)) {
        nextSection(state, cameras);
    }

    if (pressed(app, kSettingsKey)) {
        state.showSettings = !state.showSettings;
    }

    // Not through pressed(), so the editor can be closed while one of its own
    // text fields has focus.
    if (app.keyPressed[kThemeEditorKey]) {
        state.showThemeEditor = !state.showThemeEditor;
    }
}

int run() {
    AppInfo info;
    info.title = "Eastern Edge Frontend";
    info.id = kAppId;
    info.version = EER_VERSION;
    info.fileName = kFileName;
    info.maximized = true;
    info.shaderFormats = videoShaderFormats();

#if defined(EER_WINDOW_ICON)
    info.iconPng = kWindowIconPng;
    info.iconPngSize = kWindowIconPng_len;
#endif

    App app;

    if (!appInit(app, info)) {
        return -1;
    }

    if (!initGpuShared(app.gpu)) {
        reportFatal("Failed to create the video pipeline", SDL_GetError());
        appShutdown(app);
        return -1;
    }

    State state;
    state.configPath = paths::configFile(kFileName);
    state.configStatus = loadConfig(state.configPath, state.config)
                             ? "Loaded"
                             : "Using defaults";
    state.sessionDir = paths::picturesDir() / "eer_captures" / timestampForPath();

    std::cout << "Config: " << state.configPath << "\n"
              << "Captures: " << state.sessionDir << std::endl;

    image::Texture noSignal;
    noSignal.load(app.gpu, nosignal_jpg, nosignal_jpg_len);

    Cameras cameras;

    for (int i = 0; i < kCameraCount; ++i) {
        const CameraConfig& config = state.config.cameras[i];

        cameras.push_back(std::make_unique<Camera>(i + 1, noSignal.id()));
        cameras[i]->exif = config.exif;
        cameras[i]->setEndpoint(config.url, config.videoCaps, config.audioCaps);
        cameras[i]->start();
    }

    applySection(state, cameras);

    while (app.running) {
        if (!appBeginFrame(app)) {
            continue;
        }

        applyCameraSettings(state, cameras);

        // Uploads and converts the newest frame of each stream.
        for (auto& camera : cameras) {
            camera->prepare(app.cmd);
        }

        handleShortcuts(app, state, cameras);
        drawMenuBar(state);

        const float bottomBarHeight = drawBottomBar(state, cameras);

        if (state.showCameras) {
            drawCameraGrid(cameras, bottomBarHeight);
        }

        if (state.showSettings) {
            drawSettings(state, cameras, app.gpu);
        }

        if (state.showThemeEditor) {
            drawThemeEditor(&state.showThemeEditor);
        }

        appEndFrame(app);
    }

    // Everything that holds a GPU resource has to go before the device does.
    SDL_WaitForGPUIdle(app.gpu);
    cameras.clear();
    noSignal.reset();
    shutdownGpuShared();
    appShutdown(app);

    return 0;
}

}  // namespace

// SDL_main.h renames this on Windows and supplies the real WinMain, so the
// signature has to be exactly this.
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    return run();
}
