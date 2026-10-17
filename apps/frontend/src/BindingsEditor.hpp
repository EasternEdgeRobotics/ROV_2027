#pragma once

#include "Controls.hpp"
#include "Profiles.hpp"

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

    // Set once the bindings change under the pilot's hands, by a capture or a
    // profile switch, until everything is let go. Otherwise a held button
    // would read as a fresh press, and binding invert controls would invert
    // them.
    bool releasing = false;

    // The profile picked while the current one had unsaved changes, waiting
    // on whether to discard them. Empty is None.
    std::optional<std::string> pendingSwitch;

    // The name field of the New and Rename popups.
    char name[kMaxProfileName + 1] = "";
    std::string nameProblem;
    int newFrom = 0;

    std::string status;
};

// True while capturing or waiting for the pilot to let go. The app should
// ignore the controls until it is false. Call it every frame.
bool holdsControls(BindingsEditor& editor, const App& app);

// Switches to `name`, empty for None, asking first if the current profile has
// unsaved changes.
void requestProfileSwitch(BindingsEditor& editor, Profiles& profiles, const std::string& name);

// The question requestProfileSwitch asks. Draw it every frame outside any
// window, since a switch can come from the menu bar with Settings closed.
void drawProfileSwitchPopup(BindingsEditor& editor, Profiles& profiles);

// The profile picker and its buttons, then the bindings. Rows are actions,
// grouped under headings, and binding one means pressing the input you want.
// Edits `profiles.current` in place, and reads `controls` for each action's
// live value.
void drawBindingsEditor(
    BindingsEditor& editor, Profiles& profiles, const App& app, const Controls& controls);
