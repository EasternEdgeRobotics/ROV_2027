#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

// The window, the GPU device and the ImGui frame, shared by every app.
//
//   App app;
//   if (!appInit(app, info)) return -1;
//   while (app.running) {
//       if (!appBeginFrame(app)) continue;
//       ... GPU work on app.cmd, then ImGui calls ...
//       appEndFrame(app);
//   }
//   appShutdown(app);

struct AppInfo {
    const char* title = "Eastern Edge";

    // Reverse DNS. On Wayland this is the app_id the compositor matches to a
    // .desktop file, so it has to equal the ID given to eer_package_app().
    const char* id = "com.easternedgerobotics.app";

    const char* version = "0.0";

    // Short name with no spaces, used for the config and ImGui ini file names.
    const char* fileName = "app";

    int width = 1280;
    int height = 800;
    bool maximized = false;

    // Shader formats the app can supply for its own pipelines. ImGui carries
    // its own for every backend, so an app with no shaders keeps the default.
    SDL_GPUShaderFormat shaderFormats =
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL |
        SDL_GPU_SHADERFORMAT_DXIL;

    // PNG bytes for the window's own icon. Only Linux needs it, the other
    // platforms take the icon from the bundle or the .exe.
    const unsigned char* iconPng = nullptr;
    size_t iconPngSize = 0;
};

// Eastern Edge blue for a pad's light bar, adjusted because the DS4 mixes
// colours badly. The real colour is (0, 122, 217).
constexpr SDL_Color kGamepadBlue = {15, 80, 217, 255};

struct App {
    SDL_Window* window = nullptr;
    SDL_GPUDevice* gpu = nullptr;
    bool running = false;

    // Valid between appBeginFrame and appEndFrame. Upload and render to
    // offscreen targets here; ImGui's own pass is added by appEndFrame.
    SDL_GPUCommandBuffer* cmd = nullptr;

    SDL_GPUTexture* swapchain = nullptr;

    // Keys that went down this frame, by physical position. Key repeat is
    // left out, so holding a key counts once. All keyboard input is read from
    // SDL like this and never through ImGui, so there is one input path.
    bool keyPressed[SDL_SCANCODE_COUNT] = {};

    // Keys held right now, indexed the same way. SDL's own array, valid until
    // appShutdown.
    const bool* keyDown = nullptr;

    // Every connected joystick, gamepads included, opened as it is plugged in.
    // A flight stick has no gamepad mapping, so it is only ever in this list.
    std::vector<SDL_Joystick*> joysticks;

    // The first connected device SDL has a gamepad mapping for, or null.
    SDL_Gamepad* gamepad = nullptr;

    // What the gamepad's light bar shows, on pads that have one. Set it with
    // setGamepadLight, and a pad that connects later gets it too.
    SDL_Color gamepadLight = kGamepadBlue;
};

// Shows the message in a native dialog as well as on stderr, so a failure is
// readable when the app was launched by double click.
void reportFatal(const char* stage, const char* detail);

bool appInit(App& app, const AppInfo& info);

// Only sends to the pad when the colour changes, so it can be called every
// frame. Each send is a report over USB or Bluetooth.
void setGamepadLight(App& app, SDL_Color colour);

// Returns false when there is nothing to draw this frame (minimized, or
// quitting). Do not call appEndFrame in that case.
bool appBeginFrame(App& app);

void appEndFrame(App& app);

// Release every GPU resource the app owns before calling this.
void appShutdown(App& app);
