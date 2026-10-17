#include "Controls.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kHeld = 0.5f;

constexpr const char* kHatDirections[] = {"up", "right", "down", "left"};

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
    const float value = normaliseAxis(raw);

    // A throttle rests at one end, so a centre deadzone would be in the
    // middle of its useful travel.
    if (binding.fromMin) {
        return (value * binding.scale + 1.0f) * 0.5f;
    }

    return applyDeadzone(value, deadzone) * binding.scale;
}

SDL_Joystick* findJoystick(const App& app, const std::string& guid) {
    for (SDL_Joystick* joystick : app.joysticks) {
        if (joystickGuid(joystick) == guid) {
            return joystick;
        }
    }

    return nullptr;
}

// Already multiplied by the binding's scale.
float readSource(const App& app, const Binding& binding, float deadzone) {
    switch (binding.source) {
        case Source::Key:
            if (binding.index < 0 || binding.index >= SDL_SCANCODE_COUNT ||
                ImGui::GetIO().WantTextInput) {
                return 0.0f;
            }
            return app.keyDown[binding.index] ? binding.scale : 0.0f;

        case Source::Button:
            return app.gamepad &&
                           SDL_GetGamepadButton(
                               app.gamepad,
                               static_cast<SDL_GamepadButton>(binding.index))
                       ? binding.scale
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
        return SDL_GetJoystickButton(joystick, binding.index) ? binding.scale : 0.0f;
    }

    if (binding.source == Source::JoyAxis) {
        return readAxis(SDL_GetJoystickAxis(joystick, binding.index), binding, deadzone);
    }

    // SDL_HAT_UP, RIGHT, DOWN and LEFT are the bits 1, 2, 4 and 8.
    const Uint8 hat = SDL_GetJoystickHat(joystick, binding.index / 4);
    return (hat & (1 << (binding.index % 4))) ? binding.scale : 0.0f;
}

Binding make(Source source, int index, float scale) {
    Binding binding;
    binding.source = source;
    binding.index = index;
    binding.scale = scale;
    return binding;
}

Binding button(SDL_GamepadButton button, float scale = 1.0f) {
    return make(Source::Button, button, scale);
}

Binding axis(SDL_GamepadAxis axis, float scale = 1.0f) {
    return make(Source::Axis, axis, scale);
}

Binding key(SDL_Scancode key, float scale = 1.0f) {
    return make(Source::Key, key, scale);
}

void bind(Profile& profile, Action action, std::vector<Binding> bindings) {
    profile.bindings[static_cast<size_t>(action)] = std::move(bindings);
}

// The 2026 keyboard layout, except roll, which shared R with heave, and the
// motors, which were on F1 to F4 and F1 opens Settings.
Profile makeKeyboardProfile() {
    Profile profile;
    profile.name = "Keyboard";

    bind(profile, Action::Surge, {key(SDL_SCANCODE_W), key(SDL_SCANCODE_S, -1.0f)});
    bind(profile, Action::Sway, {key(SDL_SCANCODE_D), key(SDL_SCANCODE_A, -1.0f)});
    bind(profile, Action::Heave, {key(SDL_SCANCODE_R), key(SDL_SCANCODE_F, -1.0f)});
    bind(profile, Action::Yaw, {key(SDL_SCANCODE_E), key(SDL_SCANCODE_Q, -1.0f)});
    bind(profile, Action::Roll, {key(SDL_SCANCODE_H), key(SDL_SCANCODE_G, -1.0f)});
    bind(profile, Action::Servo1, {key(SDL_SCANCODE_RIGHT), key(SDL_SCANCODE_LEFT, -1.0f)});
    bind(profile, Action::Servo2, {key(SDL_SCANCODE_PAGEUP), key(SDL_SCANCODE_PAGEDOWN, -1.0f)});
    bind(profile, Action::Servo3, {key(SDL_SCANCODE_UP), key(SDL_SCANCODE_DOWN, -1.0f)});
    bind(profile, Action::Servo4, {key(SDL_SCANCODE_HOME), key(SDL_SCANCODE_END, -1.0f)});
    bind(profile, Action::Motor1, {key(SDL_SCANCODE_8), key(SDL_SCANCODE_7, -1.0f)});
    bind(profile, Action::Motor2, {key(SDL_SCANCODE_0), key(SDL_SCANCODE_9, -1.0f)});
    bind(profile, Action::Led1, {key(SDL_SCANCODE_Z), key(SDL_SCANCODE_X, -1.0f)});
    bind(profile, Action::Led2, {key(SDL_SCANCODE_J), key(SDL_SCANCODE_K, -1.0f)});
    bind(profile, Action::FastModeHold, {key(SDL_SCANCODE_LSHIFT)});
    bind(profile, Action::InvertControls, {key(SDL_SCANCODE_SPACE)});

    return profile;
}

}  // namespace

Profile gamepadProfile() {
    Profile profile;
    profile.name = "Gamepad";

    // SDL's stick Y axes are positive downwards.
    bind(profile, Action::Surge, {axis(SDL_GAMEPAD_AXIS_LEFTY, -1.0f)});
    bind(profile, Action::Sway, {axis(SDL_GAMEPAD_AXIS_LEFTX)});
    bind(profile, Action::Heave, {axis(SDL_GAMEPAD_AXIS_RIGHTY, -1.0f)});
    bind(profile, Action::Yaw, {axis(SDL_GAMEPAD_AXIS_RIGHTX)});
    bind(profile, Action::Roll, {
        button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER),
        button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, -1.0f),
    });
    bind(profile, Action::Servo1, {
        button(SDL_GAMEPAD_BUTTON_DPAD_UP),
        button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, -1.0f),
    });
    bind(profile, Action::Servo2, {
        button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT),
        button(SDL_GAMEPAD_BUTTON_DPAD_LEFT, -1.0f),
    });
    bind(profile, Action::Motor1, {
        button(SDL_GAMEPAD_BUTTON_EAST),
        button(SDL_GAMEPAD_BUTTON_SOUTH, -1.0f),
    });
    bind(profile, Action::Led1, {
        button(SDL_GAMEPAD_BUTTON_NORTH),
        button(SDL_GAMEPAD_BUTTON_WEST, -1.0f),
    });
    bind(profile, Action::FastModeHold, {axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)});
    bind(profile, Action::InvertControls, {button(SDL_GAMEPAD_BUTTON_START)});
    bind(profile, Action::Screenshot1, {button(SDL_GAMEPAD_BUTTON_BACK)});

    return profile;
}

const Profile& keyboardProfile() {
    static const Profile profile = makeKeyboardProfile();
    return profile;
}

bool keyboardModeUses(SDL_Scancode scancode) {
    for (const std::vector<Binding>& bindings : keyboardProfile().bindings) {
        for (const Binding& binding : bindings) {
            if (binding.index == scancode) {
                return true;
            }
        }
    }

    return false;
}

void Controls::update(const App& app, const Profile& profile, bool keyboardMode) {
    for (size_t i = 0; i < kActionCount; ++i) {
        float sum = 0.0f;

        for (const Binding& binding : profile.bindings[i]) {
            sum += readSource(app, binding, profile.deadzone);
        }

        if (keyboardMode) {
            for (const Binding& binding : keyboardProfile().bindings[i]) {
                sum += readSource(app, binding, 0.0f);
            }
        }

        const bool wasHeld = held_[i];

        value_[i] = std::clamp(sum, -1.0f, 1.0f);
        held_[i] = std::abs(value_[i]) >= kHeld;
        pressed_[i] = held_[i] && !wasHeld;
    }
}

float normaliseAxis(Sint16 raw) {
    return raw < 0 ? raw / 32768.0f : raw / 32767.0f;
}

std::string joystickGuid(SDL_Joystick* joystick) {
    char text[33];
    SDL_GUIDToString(SDL_GetJoystickGUID(joystick), text, sizeof(text));
    return text;
}

bool sameInput(const Binding& a, const Binding& b) {
    return a.source == b.source && a.index == b.index && a.device == b.device;
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
