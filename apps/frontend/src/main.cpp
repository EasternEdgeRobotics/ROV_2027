#include "BindingsEditor.hpp"
#include "Config.hpp"
#include "Controls.hpp"
#include "DummyRov.hpp"
#include "Pilot.hpp"
#include "Profiles.hpp"

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

#include <cstring>
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
constexpr const char* kTitle = "Eastern Edge Frontend";

// When the proper input library exists, these should be replaced with that!
constexpr SDL_Scancode kScreenshotKeys[kCameraCount] = {
    SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_F};
constexpr SDL_Scancode kCropKeys[kCameraCount] = {
    SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4};
constexpr SDL_Scancode kNextSectionKey = SDL_SCANCODE_T;
constexpr SDL_Scancode kSettingsKey = SDL_SCANCODE_F1;
constexpr SDL_Scancode kThemeEditorKey = SDL_SCANCODE_F10;

// Co-pilot keys. The keypad, because 1 to 4 already toggle the crops.
constexpr SDL_Scancode kPowerPresetKeys[] = {
    SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_3, SDL_SCANCODE_KP_4};
constexpr SDL_Scancode kPowerOffKey = SDL_SCANCODE_ESCAPE;
constexpr SDL_Scancode kFastModeKey = SDL_SCANCODE_V;

static_assert(std::size(kPowerPresetKeys) == std::size(kPowerPresets));

constexpr Action kScreenshotActions[kCameraCount] = {
    Action::Screenshot1, Action::Screenshot2, Action::Screenshot3, Action::Screenshot4};

using Cameras = std::vector<std::unique_ptr<Camera>>;

struct State {
    Config config;
    fs::path configPath;
    std::string configStatus;

    // Screenshots go to <sessionDir>/section_<n>.
    fs::path sessionDir;
    int section = 0;
    int photos = 0;  // taken in the current section

    Profiles profiles;
    BindingsEditor bindingsEditor;
    Controls controls;
    Pilot pilot;
    DummyRov dummyRov;

    // Keyboard mode SHOULD NEVER SAVE! controller/joysticks are 
    // the intended way to pilot the ROV!
    bool keyboardMode = false;

    bool showCameras = true;
    bool showSettings = false;
    bool showThemeEditor = false;
    bool showCoPilot = false;
    bool showDummyRov = false;
};

// True on the frame the key goes down. Ignored while a text field has focus,
// otherwise typing a camera URL would take screenshots. ImGui is only asked
// about focus here, the key itself comes from SDL.
bool pressed(const App& app, SDL_Scancode key) {
    return !ImGui::GetIO().WantTextInput && app.keyPressed[key];
}

// A shortcut on a key keyboard mode flies with stands aside while it is on,
// otherwise sway left would also capture camera 1.
bool shortcut(const App& app, const State& state, SDL_Scancode key) {
    return pressed(app, key) && !(state.keyboardMode && keyboardModeUses(key));
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

const char* controllerName(const App& app) {
    const char* name = nullptr;

    if (app.gamepad) {
        name = SDL_GetGamepadName(app.gamepad);
    } else if (!app.joysticks.empty()) {
        name = SDL_GetJoystickName(app.joysticks.front());
    }

    return name ? name : "No controller";
}

void drawMenuBar(const App& app, State& state) {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }

    if (ImGui::BeginMenu("Windows")) {
        // The camera view has no title bar to close it from, so this toggles.
        // windows shouldnt have checkmarks when theyre open, it looks bad.
        if (ImGui::MenuItem("Cameras")) {
            state.showCameras = !state.showCameras;
        }

        if (ImGui::MenuItem("Co-Pilot")) {
            state.showCoPilot = !state.showCoPilot;
        }

        if (ImGui::MenuItem("Dummy ROV")) {
            state.showDummyRov = !state.showDummyRov;
        }

        if (ImGui::MenuItem("Settings", SDL_GetScancodeName(kSettingsKey))) {
            state.showSettings = !state.showSettings;
        }
        ImGui::EndMenu();
    }

    // Titled with the current profile, so the menu doubles as the indicator.
    Profiles& profiles = state.profiles;
    const std::string current =
        profiles.current.name.empty() ? "No profile" : profiles.current.name;

    if (ImGui::BeginMenu((current + "###profile").c_str())) {
        // A profile copied in from another laptop shows up without a restart.
        if (ImGui::IsWindowAppearing()) {
            refreshProfiles(profiles);
        }

        // The current one is greyed out instead of ticked.
        if (ImGui::MenuItem("None", nullptr, false, !profiles.current.name.empty())) {
            requestProfileSwitch(state.bindingsEditor, profiles, "");
        }

        for (const std::string& name : profiles.names) {
            if (ImGui::MenuItem(name.c_str(), nullptr, false, name != profiles.current.name)) {
                requestProfileSwitch(state.bindingsEditor, profiles, name);
            }
        }

        ImGui::EndMenu();
    }

    ImGui::Checkbox("Keyboard mode", &state.keyboardMode);

    if (state.pilot.fastMode) {
        ImGui::TextColored(ui::kWarn, "FAST MODE");
    }

    if (state.pilot.invert) {
        ImGui::TextColored(ui::kWarn, "INVERTED");
    }

    char info[192];
    std::snprintf(
        info, sizeof(info), "%s    Section %d    Photos %d    %.0f FPS",
        controllerName(app), state.section, state.photos,
        ImGui::GetIO().Framerate);

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

void drawKeys() {
    if (!ui::beginInfoTable("keys")) {
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

    for (size_t i = 0; i < std::size(kPowerPresets); ++i) {
        const std::string text =
            std::string("Power preset: ") + kPowerPresets[i].label + ", co-pilot window open";
        ui::infoRow(SDL_GetScancodeName(kPowerPresetKeys[i]), text.c_str());
    }

    ui::infoRow(SDL_GetScancodeName(kPowerOffKey), "Power to 0%, co-pilot window open");
    ui::infoRow(SDL_GetScancodeName(kFastModeKey), "Toggle fast mode, co-pilot window open");

    ui::endInfoTable();
}

// Positive key first, so "W / S" reads as forward and back.
void drawKeyboardModeKeys() {
    if (!ui::beginInfoTable("keyboard_mode")) {
        return;
    }

    for (size_t i = 0; i < kActionCount; ++i) {
        std::string keys;

        for (const Binding& binding : keyboardProfile().bindings[i]) {
            keys += (keys.empty() ? "" : " / ") +
                    std::string(SDL_GetScancodeName(static_cast<SDL_Scancode>(binding.index)));
        }

        if (!keys.empty()) {
            ui::infoRow(keys.c_str(), kActions[i].label);
        }
    }

    ui::endInfoTable();
}

// Every connected joystick/controller
void drawDevices(const App& app) {
    if (app.joysticks.empty()) {
        ImGui::TextDisabled("No controller connected.");
        return;
    }

    for (SDL_Joystick* joystick : app.joysticks) {
        const char* name = SDL_GetJoystickName(joystick);
        const bool isGamepad = app.gamepad && SDL_GetGamepadJoystick(app.gamepad) == joystick;

        ImGui::Text("%s", name ? name : "Unnamed joystick");
        ImGui::SameLine();
        ImGui::TextDisabled(
            "%s  %s", isGamepad ? "gamepad" : "joystick", joystickGuid(joystick).c_str());
    }
}

void drawControlsTab(const App& app, State& state) {
    if (state.keyboardMode) {
        ImGui::SeparatorText("Keyboard mode");
        drawKeyboardModeKeys();
        ImGui::TextDisabled(
            "Controllers and bindings are hidden while keyboard mode is on.");
    } else {        
        ImGui::SeparatorText("Bindings");
        drawDevices(app);
        ImGui::Spacing();
        drawBindingsEditor(state.bindingsEditor, state.profiles, app, state.controls);
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Keys");
    drawKeys();
}

// For example "Eastern Edge Frontend (Peyton, unsaved)".
std::string windowTitle(const Profiles& profiles) {
    const std::string name =
        profiles.current.name.empty() ? "No profile" : profiles.current.name;

    return std::string(kTitle) + " (" + name + (profiles.unsaved() ? ", unsaved" : "") + ")";
}

// Returns true when it is fine to close. Closing is where unsaved bindings
// would be lost, and losing a layout someone just built mid-run costs them.
bool confirmQuit(const App& app, State& state) {
    Profiles& profiles = state.profiles;

    if (!profiles.unsaved()) {
        return true;
    }

    enum { kCancel, kDiscard, kSave };

    const bool named = !profiles.current.name.empty();
    const std::string message =
        named ? "Save the changes to " + profiles.current.name + "?"
              : "These bindings are not in a profile and will be lost. To keep them, "
                "cancel and use Save as in Settings > Controls.";

    const SDL_MessageBoxButtonData saveButtons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, kSave, "Save"},
        {0, kDiscard, "Discard"},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, kCancel, "Cancel"},
    };
    const SDL_MessageBoxButtonData discardButtons[] = {
        {0, kDiscard, "Discard"},
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,
         kCancel, "Cancel"},
    };

    SDL_MessageBoxData box = {};
    box.flags = SDL_MESSAGEBOX_WARNING;
    box.window = app.window;
    box.title = "Unsaved bindings";
    box.message = message.c_str();
    box.buttons = named ? saveButtons : discardButtons;
    box.numbuttons = named ? 3 : 2;

    int choice = kCancel;

    // If the dialog cannot be shown, closing anyway beats an app that cannot
    // be closed.
    if (!SDL_ShowMessageBox(&box, &choice)) {
        return true;
    }

    if (choice == kSave && !saveProfile(profiles)) {
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR, "Could not save",
            ("Could not write " + profiles.current.name + ".").c_str(), app.window);
        return false;
    }

    if (choice == kCancel && !named) {
        state.showSettings = true;
    }

    return choice != kCancel;
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

void drawSettings(const App& app, State& state, Cameras& cameras) {
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
            drawControlsTab(app, state);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("About")) {
            drawAboutTab(state, app.gpu);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

void handleShortcuts(const App& app, State& state, Cameras& cameras) {
    // The press being captured would fire its shortcut too, and Escape, which
    // cancels a capture, would cut power.
    if (state.bindingsEditor.capturing || state.bindingsEditor.releasing) {
        return;
    }

    for (int i = 0; i < kCameraCount; ++i) {
        if (shortcut(app, state, kScreenshotKeys[i]) ||
            state.controls.pressed(kScreenshotActions[i])) {
            takeScreenshot(state, cameras, i);
        }

        if (shortcut(app, state, kCropKeys[i])) {
            CropRect& crop = state.config.cameras[i].crop;
            crop.enabled = !crop.enabled;
        }
    }

    if (shortcut(app, state, kNextSectionKey)) {
        nextSection(state, cameras);
    }

    if (shortcut(app, state, kSettingsKey)) {
        state.showSettings = !state.showSettings;
    }

    // Only while the window is open, so power never changes from a stray key
    // when nobody is looking at it.
    if (state.showCoPilot) {
        for (size_t i = 0; i < std::size(kPowerPresets); ++i) {
            if (shortcut(app, state, kPowerPresetKeys[i])) {
                state.pilot.power = kPowerPresets[i].power;
            }
        }

        if (shortcut(app, state, kPowerOffKey)) {
            state.pilot.power.power = 0;
        }

        if (shortcut(app, state, kFastModeKey)) {
            state.pilot.fastToggled = !state.pilot.fastToggled;
        }
    }

    // Not through pressed(), so the editor can be closed while one of its own
    // text fields has focus.
    if (app.keyPressed[kThemeEditorKey]) {
        state.showThemeEditor = !state.showThemeEditor;
    }
}

// One frame, between appBeginFrame and appEndFrame. `title` is the window
// title last set.
void runFrame(App& app, State& state, Cameras& cameras, std::string& title) {
    // Only when it changes. Some window managers redraw the title bar on
    // every set, even to the same text.
    if (const std::string next = windowTitle(state.profiles); next != title) {
        title = next;
        SDL_SetWindowTitle(app.window, title.c_str());
    }

    applyCameraSettings(state, cameras);

    // Uploads and converts the newest frame of each stream.
    for (auto& camera : cameras) {
        camera->prepare(app.cmd);
    }

    // Before the shortcuts, which act on controller presses too, and
    // before the pilot, which reads what the shortcuts toggled.
    // Nothing is bound while a binding is being captured, so the ROV holds
    // still while someone wiggles sticks to find the one they want.
    static const Profile kNothingBound;
    const bool capturing = holdsControls(state.bindingsEditor, app);

    state.controls.update(
        app, capturing ? kNothingBound : state.profiles.current,
        state.keyboardMode && !capturing);
    handleShortcuts(app, state, cameras);
    updatePilot(state.pilot, state.controls);

    drawMenuBar(app, state);

    const float bottomBarHeight = drawBottomBar(state, cameras);

    if (state.showCameras) {
        drawCameraGrid(cameras, bottomBarHeight);
    }

    if (state.showCoPilot) {
        drawCoPilotWindow(state.pilot, &state.showCoPilot);
    }

    if (state.showDummyRov) {
        drawDummyRov(state.dummyRov, state.pilot.out, &state.showDummyRov);
    }

    if (state.showSettings) {
        drawSettings(app, state, cameras);
    }

    if (state.showThemeEditor) {
        drawThemeEditor(&state.showThemeEditor);
    }

    drawProfileSwitchPopup(state.bindingsEditor, state.profiles);
}

int run(const std::string& profileName) {
    AppInfo info;
    info.title = kTitle;
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

    // Every launch starts on None, so nobody flies on someone else's bindings
    // by accident. --profile is how a per-person launcher picks theirs.
    state.profiles.dir = paths::configDir() / "profiles";
    refreshProfiles(state.profiles);

    if (!profileName.empty() && !loadProfile(state.profiles, profileName)) {
        const std::string message =
            "There is no profile named " + profileName + " in " +
            state.profiles.dir.string() + ". Starting with no profile.";

        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_WARNING, "Profile not found", message.c_str(), app.window);
    }

    std::string title;

    std::cout << "Config: " << state.configPath << "\n"
              << "Profiles: " << state.profiles.dir << "\n"
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

    for (;;) {
        while (app.running) {
            if (appBeginFrame(app)) {
                runFrame(app, state, cameras, title);
                appEndFrame(app);
            }
        }

        // Closing is where unsaved bindings would be lost, so Cancel goes
        // back to the loop.
        if (confirmQuit(app, state)) {
            break;
        }

        app.running = true;
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
    std::string profile;

    // Anything else is ignored, macOS adds its own arguments to a bundle
    // launched from Finder.
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--profile" && i + 1 < argc) {
            profile = argv[++i];
        } else if (arg.rfind("--profile=", 0) == 0) {
            profile = arg.substr(std::strlen("--profile="));
        }
    }

    return run(profile);
}
