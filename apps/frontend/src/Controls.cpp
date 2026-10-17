#include "Controls.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kHeld = 0.5f;

constexpr const char* kHatDirections[] = {"up", "right", "down", "left"};

// SDL reports sticks as -32768..32767 and triggers as 0..32767.
float normalise(Sint16 raw) {
    return raw < 0 ? raw / 32768.0f : raw / 32767.0f;
}

// Inside the deadzone reads as zero. The rest of the travel is stretched to
// start from zero, so there is no jump at the deadzone's edge.
float applyDeadzone(float value, float deadzone) {
    const float magnitude = std::abs(value);

    if (magnitude <= deadzone) {
        return 0.0f;
    }

    return std::copysign((magnitude - deadzone) / (1.0f - deadzone), value);
}

float readAxis(Sint16 raw, const Binding& binding, float deadzone) {
    const float value = normalise(raw);

    // A throttle rests at one end, so a centre deadzone would be in the
    // middle of its useful travel.
    if (binding.fromMin) {
        return (value + 1.0f) * 0.5f;
    }

    return applyDeadzone(value, deadzone);
}

SDL_Joystick* findJoystick(const App& app, const std::string& guid) {
    for (SDL_Joystick* joystick : app.joysticks) {
        if (joystickGuid(joystick) == guid) {
            return joystick;
        }
    }

    return nullptr;
}

float readSource(const App& app, const Binding& binding, float deadzone) {
    switch (binding.source) {
        case Source::Key:
            if (binding.index < 0 || binding.index >= SDL_SCANCODE_COUNT ||
                ImGui::GetIO().WantTextInput) {
                return 0.0f;
            }
            return app.keyDown[binding.index] ? 1.0f : 0.0f;

        case Source::Button:
            return app.gamepad &&
                           SDL_GetGamepadButton(
                               app.gamepad,
                               static_cast<SDL_GamepadButton>(binding.index))
                       ? 1.0f
                       : 0.0f;

        case Source::Axis:
            if (!app.gamepad) {
                return 0.0f;
            }
            return readAxis(
                SDL_GetGamepadAxis(
                    app.gamepad, static_cast<SDL_GamepadAxis>(binding.index)),
                binding, deadzone);

        case Source::JoyButton:
        case Source::JoyAxis:
        case Source::JoyHat:
            break;
    }

    SDL_Joystick* joystick = findJoystick(app, binding.device);

    if (!joystick) {
        return 0.0f;
    }

    if (binding.source == Source::JoyButton) {
        return SDL_GetJoystickButton(joystick, binding.index) ? 1.0f : 0.0f;
    }

    if (binding.source == Source::JoyAxis) {
        return readAxis(SDL_GetJoystickAxis(joystick, binding.index), binding, deadzone);
    }

    // SDL_HAT_UP, RIGHT, DOWN and LEFT are the bits 1, 2, 4 and 8.
    const Uint8 hat = SDL_GetJoystickHat(joystick, binding.index / 4);
    return (hat & (1 << (binding.index % 4))) ? 1.0f : 0.0f;
}

Binding button(SDL_GamepadButton button, float scale = 1.0f) {
    Binding binding;
    binding.source = Source::Button;
    binding.index = button;
    binding.scale = scale;
    return binding;
}

Binding axis(SDL_GamepadAxis axis, float scale = 1.0f) {
    Binding binding;
    binding.source = Source::Axis;
    binding.index = axis;
    binding.scale = scale;
    return binding;
}

}  // namespace

Profile gamepadProfile() {
    Profile profile;
    profile.name = "Gamepad";

    auto bind = [&](Action action, std::vector<Binding> bindings) {
        profile.bindings[static_cast<size_t>(action)] = std::move(bindings);
    };

    // SDL's stick Y axes are positive downwards.
    bind(Action::Surge, {axis(SDL_GAMEPAD_AXIS_LEFTY, -1.0f)});
    bind(Action::Sway, {axis(SDL_GAMEPAD_AXIS_LEFTX)});
    bind(Action::Heave, {axis(SDL_GAMEPAD_AXIS_RIGHTY, -1.0f)});
    bind(Action::Yaw, {axis(SDL_GAMEPAD_AXIS_RIGHTX)});
    bind(Action::Roll, {
        button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER),
        button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, -1.0f),
    });
    bind(Action::Servo1, {
        button(SDL_GAMEPAD_BUTTON_DPAD_UP),
        button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, -1.0f),
    });
    bind(Action::Servo2, {
        button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT),
        button(SDL_GAMEPAD_BUTTON_DPAD_LEFT, -1.0f),
    });
    bind(Action::Led1, {
        button(SDL_GAMEPAD_BUTTON_NORTH),
        button(SDL_GAMEPAD_BUTTON_WEST, -1.0f),
    });
    bind(Action::Motor1, {
        button(SDL_GAMEPAD_BUTTON_EAST),
        button(SDL_GAMEPAD_BUTTON_SOUTH, -1.0f),
    });
    bind(Action::FastModeHold, {axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)});
    bind(Action::InvertControls, {button(SDL_GAMEPAD_BUTTON_START)});
    bind(Action::Screenshot1, {button(SDL_GAMEPAD_BUTTON_BACK)});

    return profile;
}

void Controls::update(const App& app, const Profile& profile) {
    for (size_t i = 0; i < kActionCount; ++i) {
        float sum = 0.0f;

        for (const Binding& binding : profile.bindings[i]) {
            sum += readSource(app, binding, profile.deadzone) * binding.scale;
        }

        const bool wasHeld = held_[i];

        value_[i] = std::clamp(sum, -1.0f, 1.0f);
        held_[i] = std::abs(value_[i]) >= kHeld;
        pressed_[i] = held_[i] && !wasHeld;
    }
}

std::string joystickGuid(SDL_Joystick* joystick) {
    char text[33];
    SDL_GUIDToString(SDL_GetJoystickGUID(joystick), text, sizeof(text));
    return text;
}

std::string bindingLabel(const App& app, const Binding& binding) {
    std::string text = binding.scale < 0.0f ? "-" : "";

    switch (binding.source) {
        case Source::Key:
            return text + SDL_GetScancodeName(static_cast<SDL_Scancode>(binding.index));

        // Both return null for a number SDL does not know.
        case Source::Button: {
            const char* name = SDL_GetGamepadStringForButton(
                static_cast<SDL_GamepadButton>(binding.index));
            return text + (name ? name : "unknown button");
        }

        case Source::Axis: {
            const char* name = SDL_GetGamepadStringForAxis(
                static_cast<SDL_GamepadAxis>(binding.index));
            return text + (name ? name : "unknown axis");
        }

        case Source::JoyButton:
            text += "Button " + std::to_string(binding.index);
            break;

        case Source::JoyAxis:
            text += "Axis " + std::to_string(binding.index);
            break;

        case Source::JoyHat:
            text += "Hat " + std::to_string(binding.index / 4) + " " +
                    kHatDirections[binding.index % 4];
            break;
    }

    SDL_Joystick* joystick = findJoystick(app, binding.device);
    const char* name = joystick ? SDL_GetJoystickName(joystick) : nullptr;

    return text + (name ? std::string(" on ") + name : " (not connected)");
}
