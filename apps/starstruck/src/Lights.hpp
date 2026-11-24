#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

// The lights script, run as a child process. It is sent one brightness from 0
// to 255 per line on stdin and does whatever the vehicle needs with it, so
// StarStruck knows nothing about ROS or the ROV.
class Lights {
public:
    ~Lights() { stop(); }

    // Stops any running script first. False if it could not be started.
    bool start(const std::vector<std::string>& command);

    // Sends 0, closes the script's stdin and waits briefly for it to exit.
    void stop();

    // Only writes when the level changes, so it can be called every frame.
    void set(int brightness);

    bool running();

    // "Not configured", "Running", "Exited with code 1", ...
    const std::string& status() const { return status_; }

private:
    SDL_Process* process = nullptr;
    int sent = -1;
    std::string status_ = "Not configured";
};
