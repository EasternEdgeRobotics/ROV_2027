#include <eer/app/App.hpp>

#include <eer/app/Image.hpp>
#include <eer/app/Paths.hpp>
#include <eer/app/Theme.hpp>

#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlgpu3.h"

#include <algorithm>
#include <iostream>
#include <iterator>

namespace {

void setWindowIcon(SDL_Window* window, const AppInfo& info) {
    image::Bitmap icon;

    if (!info.iconPng || !image::decode(info.iconPng, info.iconPngSize, icon)) {
        return;
    }

    SDL_Surface* surface = SDL_CreateSurfaceFrom(
        icon.width, icon.height, SDL_PIXELFORMAT_RGBA32,
        icon.pixels.data(), icon.width * 4);

    if (!surface) {
        return;
    }

    // SDL keeps its own copy, so the surface can go straight away.
    SDL_SetWindowIcon(window, surface);
    SDL_DestroySurface(surface);
}

void openJoystick(App& app, SDL_JoystickID id) {
    for (SDL_Joystick* joystick : app.joysticks) {
        if (SDL_GetJoystickID(joystick) == id) {
            return;
        }
    }

    if (SDL_Joystick* joystick = SDL_OpenJoystick(id)) {
        app.joysticks.push_back(joystick);
    }

    if (!app.gamepad && SDL_IsGamepad(id)) {
        app.gamepad = SDL_OpenGamepad(id);
    }
}

void closeJoystick(App& app, SDL_JoystickID id) {
    if (app.gamepad && SDL_GetGamepadID(app.gamepad) == id) {
        SDL_CloseGamepad(app.gamepad);
        app.gamepad = nullptr;
    }

    for (auto it = app.joysticks.begin(); it != app.joysticks.end(); ++it) {
        if (SDL_GetJoystickID(*it) == id) {
            SDL_CloseJoystick(*it);
            app.joysticks.erase(it);
            break;
        }
    }

    // A second gamepad takes over when the first is unplugged.
    for (SDL_Joystick* joystick : app.joysticks) {
        const SDL_JoystickID other = SDL_GetJoystickID(joystick);

        if (!app.gamepad && SDL_IsGamepad(other)) {
            app.gamepad = SDL_OpenGamepad(other);
        }
    }
}

}  // namespace

void reportFatal(const char* stage, const char* detail) {
    std::cerr << stage << ": " << detail << std::endl;
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, stage, detail, nullptr);
}

bool appInit(App& app, const AppInfo& info) {
    SDL_SetAppMetadata(info.title, info.version, info.id);
    SDL_SetHint(SDL_HINT_APP_ID, info.id);

    // macOS's GameController framework seems to claim flight sticks
    // (Logitech Extreme 3D), but something along the chain is broken so with SDL's
    // GameController backend enabled it doesn't work anymore.
    //
    // Remove this when https://github.com/libsdl-org/SDL/issues/16519 is fixed.
    SDL_SetHint(SDL_HINT_JOYSTICK_MFI, "0");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
        reportFatal("SDL_Init failed", SDL_GetError());
        return false;
    }

    app.keyDown = SDL_GetKeyboardState(nullptr);

    // Open what is already plugged in, so it works without waiting for an
    // added event. openJoystick skips a device that is open already, in case
    // one arrives anyway.
    int count = 0;

    if (SDL_JoystickID* ids = SDL_GetJoysticks(&count)) {
        for (int i = 0; i < count; ++i) {
            openJoystick(app, ids[i]);
        }

        SDL_free(ids);
    }

    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;

    if (info.maximized) {
        flags |= SDL_WINDOW_MAXIMIZED;
    }

    app.window = SDL_CreateWindow(info.title, info.width, info.height, flags);

    if (!app.window) {
        reportFatal("SDL_CreateWindow failed", SDL_GetError());
        SDL_Quit();
        return false;
    }

    setWindowIcon(app.window, info);

    // The D3D12 debug layer needs the "Graphics Tools" Windows feature, and
    // asking for it without that installed fails device creation outright.
#if defined(NDEBUG)
    const bool gpuDebug = false;
#else
    const bool gpuDebug = true;
#endif

    app.gpu = SDL_CreateGPUDevice(info.shaderFormats, gpuDebug, nullptr);

    if (!app.gpu) {
        reportFatal("SDL_CreateGPUDevice failed", SDL_GetError());
        SDL_DestroyWindow(app.window);
        SDL_Quit();
        return false;
    }

    if (!SDL_ClaimWindowForGPUDevice(app.gpu, app.window)) {
        reportFatal("SDL_ClaimWindowForGPUDevice failed", SDL_GetError());
        SDL_DestroyGPUDevice(app.gpu);
        SDL_DestroyWindow(app.window);
        SDL_Quit();
        return false;
    }

    SDL_SetGPUSwapchainParameters(
        app.gpu, app.window,
        SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = paths::imguiIniPath(info.fileName);
    ImGui::StyleColorsDark();
    applyTheme();

    ImGui_ImplSDL3_InitForSDLGPU(app.window);

    ImGui_ImplSDLGPU3_InitInfo initInfo = {};
    initInfo.Device = app.gpu;
    initInfo.ColorTargetFormat =
        SDL_GetGPUSwapchainTextureFormat(app.gpu, app.window);
    initInfo.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    ImGui_ImplSDLGPU3_Init(&initInfo);

    app.running = true;
    return true;
}

bool appBeginFrame(App& app) {
    std::fill(std::begin(app.keyPressed), std::end(app.keyPressed), false);

    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);

        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            app.keyPressed[event.key.scancode] = true;
        }

        // Fires for gamepads too, every gamepad is also a joystick.
        if (event.type == SDL_EVENT_JOYSTICK_ADDED) {
            openJoystick(app, event.jdevice.which);
        }

        if (event.type == SDL_EVENT_JOYSTICK_REMOVED) {
            closeJoystick(app, event.jdevice.which);
        }

        if (event.type == SDL_EVENT_QUIT) {
            app.running = false;
        }

        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
            event.window.windowID == SDL_GetWindowID(app.window)) {
            app.running = false;
        }
    }

    if (!app.running) {
        return false;
    }

    if (SDL_GetWindowFlags(app.window) & SDL_WINDOW_MINIMIZED) {
        SDL_Delay(10);
        return false;
    }

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();

    app.cmd = SDL_AcquireGPUCommandBuffer(app.gpu);

    // Wait for the swapchain here, before the caller samples any video. This
    // blocks for most of a display interval (about 8ms at 120Hz), and a video
    // frame picked up before it would sit in memory going stale that whole
    // time. Waiting first means the frames drawn are the freshest available.
    app.swapchain = nullptr;
    SDL_WaitAndAcquireGPUSwapchainTexture(
        app.cmd, app.window, &app.swapchain, nullptr, nullptr);

    ImGui::NewFrame();
    return true;
}

void appEndFrame(App& app) {
    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();

    if (app.swapchain) {
        ImGui_ImplSDLGPU3_PrepareDrawData(drawData, app.cmd);

        // Only visible where no window covers it, so it takes the chrome
        // colour and not a black that reads as a letterbox.
        const ImVec4 clear = ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg);

        SDL_GPUColorTargetInfo target = {};
        target.texture = app.swapchain;
        target.load_op = SDL_GPU_LOADOP_CLEAR;
        target.store_op = SDL_GPU_STOREOP_STORE;
        target.clear_color = SDL_FColor{clear.x, clear.y, clear.z, 1.0f};

        SDL_GPURenderPass* pass =
            SDL_BeginGPURenderPass(app.cmd, &target, 1, nullptr);
        ImGui_ImplSDLGPU3_RenderDrawData(drawData, app.cmd, pass);
        SDL_EndGPURenderPass(pass);
    }

    SDL_SubmitGPUCommandBuffer(app.cmd);
    app.cmd = nullptr;
}

void appShutdown(App& app) {
    SDL_WaitForGPUIdle(app.gpu);

    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    if (app.gamepad) {
        SDL_CloseGamepad(app.gamepad);
    }

    for (SDL_Joystick* joystick : app.joysticks) {
        SDL_CloseJoystick(joystick);
    }

    SDL_ReleaseWindowFromGPUDevice(app.gpu, app.window);
    SDL_DestroyGPUDevice(app.gpu);
    SDL_DestroyWindow(app.window);
    SDL_Quit();

    app = App{};
}
