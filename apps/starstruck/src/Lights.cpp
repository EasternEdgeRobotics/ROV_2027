#include "Lights.hpp"

#include <iostream>

bool Lights::start(const std::vector<std::string>& command) {
    stop();

    if (command.empty()) {
        status_ = "Not configured";
        return false;
    }

    std::vector<const char*> args;

    for (const std::string& arg : command) {
        args.push_back(arg.c_str());
    }

    args.push_back(nullptr);

    // Output is inherited, so the script's errors land in the same terminal
    // as ours.
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args.data());
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_INHERITED);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_INHERITED);

    process = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);

    if (!process) {
        status_ = std::string("Could not start: ") + SDL_GetError();
        std::cerr << "Lights: " << status_ << std::endl;
        return false;
    }

    sent = -1;
    status_ = "Running";
    return true;
}

void Lights::stop() {
    if (!process) {
        return;
    }

    set(0);

    // set() finds out if it had already exited.
    if (!process) {
        return;
    }

    // Closing stdin is the script's signal to turn the lights off and exit.
    // It gets a second to do that before it is asked to stop.
    SDL_CloseIO(SDL_GetProcessInput(process));

    for (int i = 0; i < 20 && !SDL_WaitProcess(process, false, nullptr); ++i) {
        SDL_Delay(50);
    }

    SDL_KillProcess(process, false);
    SDL_DestroyProcess(process);
    process = nullptr;
    status_ = "Stopped";
}

void Lights::set(int brightness) {
    if (!running() || brightness == sent) {
        return;
    }

    const std::string line = std::to_string(brightness) + "\n";
    SDL_IOStream* input = SDL_GetProcessInput(process);

    if (input && SDL_WriteIO(input, line.data(), line.size()) == line.size()) {
        SDL_FlushIO(input);
        sent = brightness;
    }
}

bool Lights::running() {
    if (!process) {
        return false;
    }

    int code = 0;

    if (SDL_WaitProcess(process, false, &code)) {
        SDL_DestroyProcess(process);
        process = nullptr;
        status_ = "Exited with code " + std::to_string(code);
        return false;
    }

    return true;
}
