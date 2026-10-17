#pragma once

#include "Controls.hpp"

// Percentages set by the co-pilot. `power` scales every axis, and each axis
// has its own on top of that.
struct Power {
    int power = 50;
    int surge = 50;
    int sway = 50;
    int heave = 50;
    int roll = 50;
    int yaw = 50;
};

struct PowerPreset {
    const char* label;
    Power power;
};

inline constexpr PowerPreset kPowerPresets[] = {
    {"All 0%", {0, 0, 0, 0, 0, 0}},
    {"All 50%", {50, 50, 50, 50, 50, 50}},
    {"Heave 100%, rest 0%", {100, 0, 0, 100, 0, 0}},
    {"Heave 75%, yaw 30%", {50, 50, 50, 75, 50, 30}},
};

// What the pilot is asking the ROV to do. Thrust axes and motors are -1..1,
// with power already applied. Servos and LEDs are 0..255.
struct PilotOutput {
    float surge = 0.0f;
    float sway = 0.0f;
    float heave = 0.0f;
    float yaw = 0.0f;
    float roll = 0.0f;
    int servo[4] = {127, 127, 127, 127};
    int led[2] = {0, 0};
    float motor[2] = {0.0f, 0.0f};
};

struct Pilot {
    Power power;
    Power fastPower = {75, 75, 50, 75, 0, 30};

    bool fastToggled = false;
    bool invert = false;

    // Toggled or held, this frame.
    bool fastMode = false;

    PilotOutput out;
};

void updatePilot(Pilot& pilot, const Controls& controls);

void drawCoPilotWindow(Pilot& pilot, bool* open);
