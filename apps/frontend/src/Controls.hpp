#pragma once

#include <eer/app/App.hpp>

#include <cstddef>
#include <iterator>
#include <string>
#include <vector>

// Everything a pilot can bind. kActions has one row per entry, in this order.
enum class Action {
    Surge,
    Sway,
    Heave,
    Yaw,
    Roll,
    Servo1,
    Servo2,
    Servo3,
    Servo4,
    Led1,
    Led2,
    Motor1,
    Motor2,
    FastModeHold,
    FastModeToggle,
    InvertControls,
    Screenshot1,
    Screenshot2,
    Screenshot3,
    Screenshot4,
    Count,
};

constexpr size_t kActionCount = static_cast<size_t>(Action::Count);

struct ActionInfo {
    const char* code;  // what a saved profile stores, so never rename one
    const char* label;
};

inline constexpr ActionInfo kActions[] = {
    {"surge", "Surge"},
    {"sway", "Sway"},
    {"heave", "Heave"},
    {"yaw", "Yaw"},
    {"roll", "Roll"},
    {"servo_1", "Servo 1"},
    {"servo_2", "Servo 2"},
    {"servo_3", "Servo 3"},
    {"servo_4", "Servo 4"},
    {"led_1", "LED 1"},
    {"led_2", "LED 2"},
    {"motor_1", "Motor 1"},
    {"motor_2", "Motor 2"},
    {"fast_mode_hold", "Fast mode (hold)"},
    {"fast_mode_toggle", "Fast mode (toggle)"},
    {"invert_controls", "Invert controls"},
    {"screenshot_1", "Capture camera 1"},
    {"screenshot_2", "Capture camera 2"},
    {"screenshot_3", "Capture camera 3"},
    {"screenshot_4", "Capture camera 4"},
};

static_assert(std::size(kActions) == kActionCount);

enum class Source {
    Key,        // SDL_Scancode
    Button,     // SDL_GamepadButton
    Axis,       // SDL_GamepadAxis
    JoyButton,  // raw joystick button number
    JoyAxis,    // raw joystick axis number
    JoyHat,     // hat * 4 + direction, directions in the order up, right, down, left
};

struct Binding {
    Source source = Source::Key;
    int index = 0;

    // -1 makes the input push the action the other way, which is how one
    // action covers both directions.
    float scale = 1.0f;

    // For an axis that stays where it is left, like a throttle. Reads its full
    // travel as 0..1 instead of -1..1.
    bool fromMin = false;

    // The joystick's GUID, for the Joy sources only. Their numbers mean
    // something different on every model.
    std::string device;
};

struct Profile {
    std::string name;
    float deadzone = 0.15f;
    std::vector<Binding> bindings[kActionCount];
};

// A standard gamepad layout. Used until profiles can be loaded from disk.
Profile gamepadProfile();

// Every action's state this frame. An action is held while its value is at
// least half way in either direction, and pressed on the frame it becomes held.
class Controls {
public:
    void update(const App& app, const Profile& profile);

    float value(Action action) const { return value_[index(action)]; }
    bool held(Action action) const { return held_[index(action)]; }
    bool pressed(Action action) const { return pressed_[index(action)]; }

private:
    static size_t index(Action action) { return static_cast<size_t>(action); }

    float value_[kActionCount] = {};
    bool held_[kActionCount] = {};
    bool pressed_[kActionCount] = {};
};

std::string joystickGuid(SDL_Joystick* joystick);

// Short text for the Controls tab, for example "-lefty" or "Hat 0 up".
std::string bindingLabel(const App& app, const Binding& binding);
