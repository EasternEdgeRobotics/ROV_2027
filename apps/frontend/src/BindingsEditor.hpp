#pragma once

#include "Controls.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

struct BindingsEditor {
    // Set while waiting for the pilot to press something.
    std::optional<Action> capturing;

    // Capture only starts once everything is released, so the press that
    // opened it is not what gets bound.
    bool armed = false;

    // Where each raw joystick axis sat when capture armed. A throttle rests at
    // one end, so movement is measured from here and not from zero.
    std::vector<std::pair<SDL_Joystick*, std::vector<float>>> rest;

    // Set once a binding is captured, until the input is let go. Otherwise
    // the button just bound would read as a fresh press, and binding invert
    // controls would invert them.
    bool releasing = false;

    std::string status;
};

// True while capturing or waiting for the captured input to be let go. The
// app should ignore the controls until it is false. Call it every frame.
bool holdsControls(BindingsEditor& editor, const App& app);

// Rows are actions, grouped under headings. Binding one means pressing the
// input you want. Edits `profile` in place, and reads `controls` for each
// action's live value.
void drawBindingsEditor(
    BindingsEditor& editor, Profile& profile, const App& app, const Controls& controls);
