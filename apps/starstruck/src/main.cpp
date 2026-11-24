#include "Config.hpp"
#include "Lights.hpp"

#include <eer/app/App.hpp>
#include <eer/app/Image.hpp>
#include <eer/app/Paths.hpp>
#include <eer/app/Theme.hpp>
#include <eer/app/Ui.hpp>
#include <eer/video/Camera.hpp>
#include <eer/video/GpuShared.hpp>

#include <SDL3/SDL_main.h>
#include "imgui.h"

#include "build_info.h"
#include "nosignal_jpg.h"

#if defined(EER_WINDOW_ICON)
#include "window_icon.h"
#endif

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Must match the ID in CMakeLists.txt.
constexpr const char* kAppId = "com.easternedgerobotics.starstruck";
constexpr const char* kFileName = "starstruck";
constexpr const char* kTitle = "StarStruck";

constexpr SDL_Scancode kShootKey = SDL_SCANCODE_SPACE;
constexpr SDL_Scancode kPrintKey = SDL_SCANCODE_RETURN;
constexpr SDL_Scancode kCancelKey = SDL_SCANCODE_ESCAPE;
constexpr SDL_Scancode kMoreCopiesKey = SDL_SCANCODE_UP;
constexpr SDL_Scancode kFewerCopiesKey = SDL_SCANCODE_DOWN;
constexpr SDL_Scancode kSettingsKey = SDL_SCANCODE_F1;
constexpr SDL_Scancode kFullscreenKey = SDL_SCANCODE_F11;

// A camera that has not delivered the frame by then has stalled, and the
// booth goes back to live rather than wait on it.
constexpr Uint64 kCaptureTimeoutMs = 2000;

// Enough for a group, without one group emptying the paper tray.
constexpr int kMaxCopies = 10;

// How long the white flash takes to fade once the photo is taken.
constexpr Uint64 kScreenFlashMs = 400;

// One breath of the idle lights, in and out.
constexpr double kBreathMs = 4000.0;
constexpr double kPi = 3.14159265358979323846;

enum class Stage {
    Live,
    Countdown,
    Flash,      // lights at full, waiting for them to show on camera
    Capturing,  // screenshot asked for, waiting on the next frame
    Review      // the photo is held on screen
};

struct State {
    Config config;
    fs::path configPath;
    std::string configStatus;

    fs::path sessionDir;

    Stage stage = Stage::Live;
    Uint64 stageStart = 0;

    std::string message;
    int copies = 1;  // for the photo under review
    int queued = 0;  // photos in the print folder
    Uint64 lastQueueCheck = 0;

    bool showSettings = false;
};

// True on the frame the key goes down. Ignored while a text field has focus,
// otherwise typing a camera URL with a space in it would take a photo.
bool pressed(const App& app, SDL_Scancode key) {
    return !ImGui::GetIO().WantTextInput && app.keyPressed[key];
}

fs::path printDir(const Config& config) {
    return config.printDir[0] ? fs::path(config.printDir)
                              : paths::picturesDir() / "starstruck" / "print";
}

void setStage(State& state, Stage stage) {
    state.stage = stage;
    state.stageStart = SDL_GetTicks();
}

void shoot(State& state, const Camera& camera) {
    if (camera.status() != Camera::Status::Live) {
        state.message = "The camera is not live";
        return;
    }

    state.message.clear();
    setStage(state, Stage::Countdown);
}

void backToLive(State& state, Camera& camera) {
    camera.hold = false;
    setStage(state, Stage::Live);
}

// Straight into a new countdown, for when someone blinked.
void retake(State& state, Camera& camera) {
    backToLive(state, camera);
    shoot(state, camera);
}

void changeCopies(State& state, int by) {
    state.copies = std::clamp(state.copies + by, 1, kMaxCopies);
}

// Copies the held photo into the print folder, once per copy. Each goes in
// under a .part name and is renamed once complete, so the print script never
// sees half a file.
void print(State& state, Camera& camera) {
    camera.waitForScreenshotWrites();

    const fs::path source = camera.lastScreenshotFile();
    const fs::path dir = printDir(state.config);

    std::error_code ec;
    fs::create_directories(dir, ec);

    for (int i = 1; i <= state.copies && !ec; ++i) {
        const std::string name = source.stem().string() +
                                 (i > 1 ? "_copy" + std::to_string(i) : "") +
                                 source.extension().string();
        const fs::path partial = dir / (name + ".part");

        fs::copy_file(source, partial, fs::copy_options::overwrite_existing, ec);

        if (!ec) {
            fs::rename(partial, dir / name, ec);
        }
    }

    if (ec) {
        state.message = "Could not send to the printer: " + ec.message();
    } else if (state.copies == 1) {
        state.message = "Sent to the printer";
    } else {
        state.message = "Sent " + std::to_string(state.copies) + " copies to the printer";
    }

    backToLive(state, camera);
}

// Moves the booth along and returns the light level for this frame.
int updateBooth(State& state, Camera& camera) {
    const Config& config = state.config;
    const Uint64 elapsed = SDL_GetTicks() - state.stageStart;

    switch (state.stage) {
        case Stage::Countdown:
            if (elapsed >= Uint64(config.countdownSeconds) * 1000) {
                setStage(state, Stage::Flash);
                return config.flashBrightness;
            }

            return elapsed % 1000 < 500 ? config.blinkBrightness : 0;

        case Stage::Flash:
            if (elapsed >= Uint64(config.flashLeadMs)) {
                if (camera.screenshot()) {
                    setStage(state, Stage::Capturing);
                } else {
                    state.message = "Could not save the photo";
                    setStage(state, Stage::Live);
                }
            }

            return config.flashBrightness;

        // prepare() has already run this frame, so if the screenshot is no
        // longer pending, the picture on screen is the one that was saved.
        case Stage::Capturing:
            if (!camera.screenshotPending()) {
                camera.hold = true;
                state.copies = 1;
                setStage(state, Stage::Review);
            } else if (elapsed > kCaptureTimeoutMs) {
                state.message = "The camera sent no picture";
                setStage(state, Stage::Live);
            }

            return config.flashBrightness;

        // Starts from idle and eases in and out, so it never jumps.
        case Stage::Live:
            if (const Uint64 after = Uint64(config.breatheAfterSeconds) * 1000;
                elapsed >= after && config.breatheBrightness > config.idleBrightness) {
                const double phase = double((elapsed - after) % Uint64(kBreathMs)) / kBreathMs;
                const double amount = 0.5 - 0.5 * std::cos(phase * 2.0 * kPi);

                return config.idleBrightness +
                       int(amount * (config.breatheBrightness - config.idleBrightness) + 0.5);
            }

            break;

        case Stage::Review:
            break;
    }

    return config.idleBrightness;
}

void countQueued(State& state) {
    const Uint64 now = SDL_GetTicks();

    if (now - state.lastQueueCheck < 1000) {
        return;
    }

    state.lastQueueCheck = now;
    state.queued = 0;

    std::error_code ec;

    for (const auto& entry : fs::directory_iterator(printDir(state.config), ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".tiff") {
            ++state.queued;
        }
    }
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

void drawMenuBar(State& state, Camera& camera, Lights& lights) {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }

    if (ImGui::MenuItem("Settings", SDL_GetScancodeName(kSettingsKey))) {
        state.showSettings = !state.showSettings;
    }

    const std::string cameraText = "Camera: " + camera.statusText();
    const std::string lightsText = "Lights: " + lights.status();
    const std::string queueText = std::to_string(state.queued) + " waiting to print";
    const ImGuiStyle& style = ImGui::GetStyle();

    ui::alignRight(
        ImGui::CalcTextSize(cameraText.c_str()).x +
        ImGui::CalcTextSize(lightsText.c_str()).x +
        ImGui::CalcTextSize(queueText.c_str()).x + style.ItemSpacing.x * 2.0f);

    ImGui::TextColored(statusColour(camera.status()), "%s", cameraText.c_str());
    ImGui::TextColored(lights.running() ? ui::kGood : ui::kIdle, "%s", lightsText.c_str());
    ImGui::TextUnformatted(queueText.c_str());

    ImGui::EndMainMenuBar();
}

// A row of large buttons centred along the bottom. Returns its height.
float drawBottomBar(State& state, Camera& camera) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float fontSize = ImGui::GetStyle().FontSizeBase * 1.6f;

    ImGui::PushFont(nullptr, fontSize);
    const float height = ImGui::GetFrameHeight() + 32.0f;

    ImGui::SetNextWindowPos(ImVec2(
        viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - height));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 16.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(24.0f, 8.0f));
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

    enum class Press { Shoot, Cancel, Back, Fewer, More, Print, Retake };

    struct BarButton {
        std::string label;
        Press press;
    };

    // The buttons for this stage. Measured first so the row can be centred.
    std::vector<BarButton> buttons;

    switch (state.stage) {
        case Stage::Live:
            buttons = {{"Take photo", Press::Shoot}};
            break;

        case Stage::Countdown:
            buttons = {{"Cancel", Press::Cancel}};
            break;

        // The ### keeps Print's ID the same while its label counts.
        case Stage::Review:
            buttons = {
                {"Back", Press::Back},
                {"-", Press::Fewer},
                {"Print " + std::to_string(state.copies) +
                     (state.copies == 1 ? " copy" : " copies") + "###print",
                 Press::Print},
                {"+", Press::More},
                {"Retake", Press::Retake},
            };
            break;

        case Stage::Flash:
        case Stage::Capturing:
            break;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    float width = 0.0f;

    for (const BarButton& button : buttons) {
        width += ImGui::CalcTextSize(button.label.c_str(), nullptr, true).x +
                 style.FramePadding.x * 2.0f + style.ItemSpacing.x;
    }

    // Only with a button to follow. ImGui reports a cursor moved past the
    // content with no item after it, which is what the flash would do.
    if (!buttons.empty()) {
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - width + style.ItemSpacing.x) * 0.5f);
    }

    for (size_t i = 0; i < buttons.size(); ++i) {
        if (i > 0) {
            ImGui::SameLine();
        }

        if (!ImGui::Button(buttons[i].label.c_str())) {
            continue;
        }

        switch (buttons[i].press) {
            case Press::Shoot:  shoot(state, camera); break;
            case Press::Fewer:  changeCopies(state, -1); break;
            case Press::More:   changeCopies(state, 1); break;
            case Press::Print:  print(state, camera); break;
            case Press::Retake: retake(state, camera); break;
            case Press::Cancel:
            case Press::Back:   backToLive(state, camera); break;
        }
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
    ImGui::PopFont();

    return height;
}

// The camera, as large as fits without stretching, with the countdown and
// any message drawn over it.
void drawBooth(State& state, Camera& camera, float bottomBarHeight) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(
        viewport->WorkSize.x, viewport->WorkSize.y - bottomBarHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

    ImGui::Begin(
        "##booth",
        nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 frame = camera.frameSize();
    ImVec2 size = avail;

    // No frame yet means the no signal image, which is 16:9.
    const float aspect = frame.y > 0.0f ? frame.x / frame.y : 16.0f / 9.0f;

    if (avail.x / avail.y > aspect) {
        size.x = avail.y * aspect;
    } else {
        size.y = avail.x / aspect;
    }

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 topLeft(
        origin.x + (avail.x - size.x) * 0.5f, origin.y + (avail.y - size.y) * 0.5f);

    ImGui::SetCursorScreenPos(topLeft);
    camera.render(size);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 centre(topLeft.x + size.x * 0.5f, topLeft.y + size.y * 0.5f);

    auto drawCentred = [&](const std::string& text, float fontSize, float y) {
        ImGui::PushFont(nullptr, fontSize);
        const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
        const ImVec2 at(centre.x - textSize.x * 0.5f, y - textSize.y * 0.5f);

        draw->AddText(ImVec2(at.x + 3.0f, at.y + 3.0f), IM_COL32(0, 0, 0, 160), text.c_str());
        draw->AddText(at, IM_COL32_WHITE, text.c_str());
        ImGui::PopFont();
    };

    if (state.stage == Stage::Countdown) {
        const Uint64 elapsed = SDL_GetTicks() - state.stageStart;
        const int left = state.config.countdownSeconds - int(elapsed / 1000);
        drawCentred(std::to_string(left), size.y * 0.4f, centre.y);
    }

    if (!state.message.empty()) {
        drawCentred(state.message, ImGui::GetStyle().FontSizeBase * 2.0f,
                    topLeft.y + size.y - 48.0f);
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

void drawSettings(State& state, Camera& camera, Lights& lights) {
    ImGui::SetNextWindowSize(ImVec2(720, 560), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Settings", &state.showSettings)) {
        ImGui::End();
        return;
    }

    Config& config = state.config;

    ImGui::SeparatorText("Camera");
    ImGui::TextDisabled(
        "A WHEP URL, for example http://192.168.137.200:8889/cam/whep. "
        "Applied when you leave a field or press Enter.");

    ui::fieldLabel("Vehicle");
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputText("##vehicle", config.vehicle, sizeof(config.vehicle));

    // Only redial once a field is left, so a half typed URL never starts a
    // connection attempt.
    bool committed = false;

    ui::fieldLabel("URL");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##url", config.url, sizeof(config.url));
    committed |= ImGui::IsItemDeactivatedAfterEdit();

#if defined(EER_VIDEO_BACKEND_WEBRTCBIN)
    ui::fieldLabel("Video caps");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##video", config.videoCaps, sizeof(config.videoCaps));
    committed |= ImGui::IsItemDeactivatedAfterEdit();

    ui::fieldLabel("Audio caps");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##audio", config.audioCaps, sizeof(config.audioCaps));
    committed |= ImGui::IsItemDeactivatedAfterEdit();
#endif

    if (committed) {
        camera.setEndpoint(config.url, config.videoCaps, config.audioCaps);
    }

    ui::fieldLabel("Flip");
    ImGui::Checkbox("Vertical", &config.flipVertical);
    ImGui::SameLine();
    ImGui::Checkbox("Horizontal", &config.flipHorizontal);

    ui::fieldLabel("Status");
    ImGui::TextColored(statusColour(camera.status()), "%s", camera.statusText().c_str());

    ImGui::SeparatorText("Countdown and lights");

    ui::fieldLabel("Countdown");
    ImGui::SetNextItemWidth(220.0f);
    ImGui::SliderInt("##countdown", &config.countdownSeconds, 1, 10, "%d s");

    ui::fieldLabel("Idle");
    ImGui::SetNextItemWidth(220.0f);
    ImGui::SliderInt("##idle", &config.idleBrightness, 0, 255);

    ui::fieldLabel("Blink");
    ImGui::SetNextItemWidth(220.0f);
    ImGui::SliderInt("##blink", &config.blinkBrightness, 0, 255);

    ui::fieldLabel("Flash");
    ImGui::SetNextItemWidth(220.0f);
    ImGui::SliderInt("##flash", &config.flashBrightness, 0, 255);

    ui::fieldLabel("Breathe");
    ImGui::SetNextItemWidth(220.0f);
    ImGui::SliderInt("##breathe", &config.breatheBrightness, 0, 255);

    ui::fieldLabel("Breathe after");
    ImGui::SetNextItemWidth(220.0f);
    ImGui::SliderInt("##breathe_after", &config.breatheAfterSeconds, 5, 600, "%d s");

    ui::fieldLabel("Flash lead");
    ImGui::SetNextItemWidth(220.0f);
    ImGui::SliderInt("##lead", &config.flashLeadMs, 0, 2000, "%d ms");

    std::string command;

    for (const std::string& arg : config.lightsCommand) {
        command += (command.empty() ? "" : " ") + arg;
    }

    ui::fieldLabel("Command");
    ImGui::TextWrapped("%s", command.empty() ? "None, set lights_command in the config file" : command.c_str());

    ui::fieldLabel("Script");
    ImGui::TextUnformatted(lights.status().c_str());
    ImGui::SameLine();

    if (ImGui::SmallButton("Restart")) {
        lights.start(config.lightsCommand);
    }

    ImGui::SeparatorText("Files");

    ui::fieldLabel("Print folder");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##print", printDir(Config{}).string().c_str(),
                             config.printDir, sizeof(config.printDir));

    ui::fieldLabel("Photos");
    ImGui::TextWrapped("%s", state.sessionDir.string().c_str());

    ui::fieldLabel("Config");
    ImGui::TextWrapped("%s", state.configPath.string().c_str());

    ImGui::Spacing();

    if (ImGui::Button("Save")) {
        state.configStatus = saveConfig(state.configPath, config) ? "Saved" : "Failed to save";
    }

    ImGui::SameLine();
    ImGui::TextDisabled("%s", state.configStatus.c_str());
    ImGui::TextDisabled("Built %s", EER_BUILD_DATE);

    ImGui::End();
}

void handleKeys(App& app, State& state, Camera& camera) {
    if (pressed(app, kShootKey)) {
        if (state.stage == Stage::Live) {
            shoot(state, camera);
        } else if (state.stage == Stage::Review) {
            retake(state, camera);
        }
    }

    if (state.stage == Stage::Review) {
        if (pressed(app, kMoreCopiesKey)) {
            changeCopies(state, 1);
        }

        if (pressed(app, kFewerCopiesKey)) {
            changeCopies(state, -1);
        }

        if (pressed(app, kPrintKey)) {
            print(state, camera);
        }
    }

    if (pressed(app, kCancelKey) &&
        (state.stage == Stage::Countdown || state.stage == Stage::Review)) {
        backToLive(state, camera);
    }

    if (pressed(app, kSettingsKey)) {
        state.showSettings = !state.showSettings;
    }

    if (app.keyPressed[kFullscreenKey]) {
        const bool fullscreen = SDL_GetWindowFlags(app.window) & SDL_WINDOW_FULLSCREEN;
        SDL_SetWindowFullscreen(app.window, !fullscreen);
    }
}

// White over everything, fading out from the moment the photo was taken. It
// starts after the capture, so it never lights the photo itself.
void drawScreenFlash(const State& state) {
    const Uint64 elapsed = SDL_GetTicks() - state.stageStart;

    if (state.stage != Stage::Review || elapsed >= kScreenFlashMs) {
        return;
    }

    const float alpha = 1.0f - float(elapsed) / float(kScreenFlashMs);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::GetForegroundDrawList()->AddRectFilled(
        viewport->Pos,
        ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y),
        ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, alpha)));
}

void runFrame(App& app, State& state, Camera& camera, Lights& lights) {
    camera.flipVertical = state.config.flipVertical;
    camera.flipHorizontal = state.config.flipHorizontal;
    camera.vehicleName = state.config.vehicle;
    camera.prepare(app.cmd);

    handleKeys(app, state, camera);
    lights.set(updateBooth(state, camera));
    countQueued(state);

    drawMenuBar(state, camera, lights);
    drawBooth(state, camera, drawBottomBar(state, camera));

    if (state.showSettings) {
        drawSettings(state, camera, lights);
    }

    drawScreenFlash(state);
}

int run() {
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
    state.configStatus = loadConfig(state.configPath, state.config) ? "Loaded" : "Using defaults";
    state.sessionDir = paths::sessionDir("starstruck");

    std::cout << "Config: " << state.configPath << "\n"
              << "Photos: " << state.sessionDir << "\n"
              << "Print folder: " << printDir(state.config) << std::endl;

    image::Texture noSignal;
    noSignal.load(app.gpu, nosignal_jpg, nosignal_jpg_len);

    Lights lights;
    lights.start(state.config.lightsCommand);

    {
        Camera camera(1, noSignal.id());
        camera.section = "starstruck";
        camera.screenshotDir = state.sessionDir;
        camera.setEndpoint(state.config.url, state.config.videoCaps, state.config.audioCaps);
        camera.start();

        while (app.running) {
            if (appBeginFrame(app)) {
                runFrame(app, state, camera, lights);
                appEndFrame(app);
            }
        }

        // The camera holds GPU resources, so it has to go before the device.
        SDL_WaitForGPUIdle(app.gpu);
    }

    lights.stop();
    noSignal.reset();
    shutdownGpuShared();
    appShutdown(app);

    return 0;
}

}  // namespace

// SDL_main.h renames this on Windows and supplies the real WinMain, so the
// signature has to be exactly this.
int main(int, char**) {
    return run();
}
