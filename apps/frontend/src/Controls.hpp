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
    Motor1,
    Motor2,
    Led1,
    Led2,
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

    // Heading in the bindings editor. Rows with the same group stay together.
    const char* group;

    // Whether the sign matters, so a binding can be reversed.
    bool directional;
};

inline constexpr ActionInfo kActions[] = {
    {"surge", "Surge", "Movement", true},
    {"sway", "Sway", "Movement", true},
    {"heave", "Heave", "Movement", true},
    {"yaw", "Yaw", "Movement", true},
    {"roll", "Roll", "Movement", true},
    {"servo_1", "Servo 1", "Manipulators", true},
    {"servo_2", "Servo 2", "Manipulators", true},
    {"servo_3", "Servo 3", "Manipulators", true},
    {"servo_4", "Servo 4", "Manipulators", true},
    {"motor_1", "Motor 1", "Manipulators", true},
    {"motor_2", "Motor 2", "Manipulators", true},
    {"led_1", "LED 1", "Lighting", true},
    {"led_2", "LED 2", "Lighting", true},
    {"fast_mode_hold", "Fast mode (hold)", "Modes", false},
    {"fast_mode_toggle", "Fast mode (toggle)", "Modes", false},
    {"invert_controls", "Invert controls", "Modes", false},
    {"screenshot_1", "Capture camera 1", "Capture", false},
    {"screenshot_2", "Capture camera 2", "Capture", false},
    {"screenshot_3", "Capture camera 3", "Capture", false},
    {"screenshot_4", "Capture camera 4", "Capture", false},
};

static_assert(std::size(kActions) == kActionCount);

enum class Source {
    Key,        // SDL_Scancode, only used by the keyboard mode layout
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
    // travel as 0..1 instead of -1..1, and `scale` picks which end is zero:
    // the -1 end when positive, the +1 end when negative.
    bool fromMin = false;

    // The joystick's GUID, for the Joy sources only. Their numbers mean
    // something different on every model.
    std::string device;

    friend bool operator==(const Binding&, const Binding&) = default;
};

struct Profile {
    std::string name;  // empty for the built-in None profile
    float deadzone = 0.15f;
    std::vector<Binding> bindings[kActionCount];

    friend bool operator==(const Profile&, const Profile&) = default;
};

// A standard gamepad layout. Used until profiles can be loaded from disk.
Profile gamepadProfile();

// The fixed keyboard layout, for testing without a controller or flying when
// one dies. Used on top of the profile while keyboard mode is on.
const Profile& keyboardProfile();

// Whether keyboard mode drives anything with this key, so the shortcut on the
// same key can stand aside while it is on.
bool keyboardModeUses(SDL_Scancode key);

// Every action's state this frame. An action is held while its value is at
// least half way in either direction, and pressed on the frame it becomes held.
class Controls {
public:
    void update(const App& app, const Profile& profile, bool keyboardMode);

    float value(Action action) const { return value_[index(action)]; }
    bool held(Action action) const { return held_[index(action)]; }
    bool pressed(Action action) const { return pressed_[index(action)]; }

private:
    static size_t index(Action action) { return static_cast<size_t>(action); }

    float value_[kActionCount] = {};
    bool held_[kActionCount] = {};
    bool pressed_[kActionCount] = {};
};

// SDL reports sticks as -32768..32767 and triggers as 0..32767. Returns -1..1.
float normaliseAxis(Sint16 raw);

std::string joystickGuid(SDL_Joystick* joystick);

// The same physical input, whatever its direction.
bool sameInput(const Binding& a, const Binding& b);

// Short text for the Controls tab, for example "-lefty" or "Hat 0 up".
std::string bindingLabel(const App& app, const Binding& binding);
