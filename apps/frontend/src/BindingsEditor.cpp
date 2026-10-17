#include "BindingsEditor.hpp"

#include <eer/app/Ui.hpp>

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

// An axis has to travel this far before capture takes it, so resting drift
// on a worn stick does not bind itself.
constexpr float kCaptureThreshold = 0.6f;

// A raw axis resting further out than this is a throttle or similar, and is
// bound so that its resting end reads zero.
constexpr float kRestsAtEnd = 0.9f;

constexpr ImVec4 kConflict{0.45f, 0.16f, 0.16f, 1.0f};

bool isGamepadJoystick(const App& app, SDL_Joystick* joystick) {
    return app.gamepad && SDL_GetGamepadJoystick(app.gamepad) == joystick;
}

Binding captured(Source source, int index, float scale, std::string device = "") {
    Binding binding;
    binding.source = source;
    binding.index = index;
    binding.scale = scale;
    binding.device = std::move(device);
    return binding;
}

// Gamepad buttons and axes, and raw joystick buttons and hats. A gamepad is
// skipped as a raw joystick, so one press cannot bind twice.
std::optional<Binding> scanPressed(const App& app) {
    if (app.gamepad) {
        for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i) {
            if (SDL_GetGamepadButton(app.gamepad, static_cast<SDL_GamepadButton>(i))) {
                return captured(Source::Button, i, 1.0f);
            }
        }

        for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; ++i) {
            const float value = normaliseAxis(
                SDL_GetGamepadAxis(app.gamepad, static_cast<SDL_GamepadAxis>(i)));

            // The direction pushed is the direction bound, so nobody has to
            // know which way SDL calls positive.
            if (std::abs(value) >= kCaptureThreshold) {
                return captured(Source::Axis, i, value < 0.0f ? -1.0f : 1.0f);
            }
        }
    }

    for (SDL_Joystick* joystick : app.joysticks) {
        if (isGamepadJoystick(app, joystick)) {
            continue;
        }

        for (int i = 0; i < SDL_GetNumJoystickButtons(joystick); ++i) {
            if (SDL_GetJoystickButton(joystick, i)) {
                return captured(Source::JoyButton, i, 1.0f, joystickGuid(joystick));
            }
        }

        for (int hat = 0; hat < SDL_GetNumJoystickHats(joystick); ++hat) {
            const Uint8 bits = SDL_GetJoystickHat(joystick, hat);

            for (int direction = 0; direction < 4; ++direction) {
                if (bits & (1 << direction)) {
                    return captured(
                        Source::JoyHat, hat * 4 + direction, 1.0f, joystickGuid(joystick));
                }
            }
        }
    }

    return std::nullopt;
}

void recordRest(BindingsEditor& editor, const App& app) {
    editor.rest.clear();

    for (SDL_Joystick* joystick : app.joysticks) {
        if (isGamepadJoystick(app, joystick)) {
            continue;
        }

        std::vector<float> axes;

        for (int i = 0; i < SDL_GetNumJoystickAxes(joystick); ++i) {
            axes.push_back(normaliseAxis(SDL_GetJoystickAxis(joystick, i)));
        }

        editor.rest.emplace_back(joystick, std::move(axes));
    }
}

std::optional<Binding> scanRawAxes(const BindingsEditor& editor, const App& app) {
    for (const auto& [joystick, rest] : editor.rest) {
        // Unplugged since capture armed.
        if (std::find(app.joysticks.begin(), app.joysticks.end(), joystick) ==
            app.joysticks.end()) {
            continue;
        }

        const int count = std::min<int>(rest.size(), SDL_GetNumJoystickAxes(joystick));

        for (int i = 0; i < count; ++i) {
            const float moved = normaliseAxis(SDL_GetJoystickAxis(joystick, i)) - rest[i];

            if (std::abs(moved) < kCaptureThreshold) {
                continue;
            }

            Binding binding = captured(
                Source::JoyAxis, i, moved < 0.0f ? -1.0f : 1.0f, joystickGuid(joystick));

            if (std::abs(rest[i]) > kRestsAtEnd) {
                binding.fromMin = true;
                binding.scale = rest[i] < 0.0f ? 1.0f : -1.0f;
            }

            return binding;
        }
    }

    return std::nullopt;
}

// The other action this input is also bound to, or null.
const char* conflictWith(const Profile& profile, size_t action, const Binding& binding) {
    for (size_t other = 0; other < kActionCount; ++other) {
        if (other == action) {
            continue;
        }

        for (const Binding& candidate : profile.bindings[other]) {
            if (sameInput(candidate, binding)) {
                return kActions[other].label;
            }
        }
    }

    return nullptr;
}

void startCapture(BindingsEditor& editor, Action action) {
    editor.capturing = action;
    editor.armed = false;
    editor.rest.clear();
}

// One row's bindings as chips. Click a chip to unbind it.
void drawChips(
    BindingsEditor& editor, Profile& profile, const App& app, size_t action) {
    std::vector<Binding>& bindings = profile.bindings[action];

    std::optional<size_t> remove;
    std::optional<size_t> flip;

    for (size_t i = 0; i < bindings.size(); ++i) {
        const Binding& binding = bindings[i];
        const char* conflict = conflictWith(profile, action, binding);

        if (i > 0) {
            ImGui::SameLine();
        }

        ImGui::PushID(static_cast<int>(i));

        if (conflict) {
            ImGui::PushStyleColor(ImGuiCol_Button, kConflict);
        }

        if (ImGui::SmallButton(bindingLabel(app, binding).c_str())) {
            remove = i;
        }

        if (conflict) {
            ImGui::PopStyleColor();
        }

        if (ImGui::IsItemHovered()) {
            if (conflict) {
                ImGui::SetTooltip("Click to unbind. Also bound to %s.", conflict);
            } else {
                ImGui::SetTooltip("Click to unbind.");
            }
        }

        if (kActions[action].directional) {
            ImGui::SameLine(0.0f, 2.0f);

            if (ImGui::SmallButton("+/-")) {
                flip = i;
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Reverse this binding's direction.");
            }
        }

        ImGui::PopID();
    }

    if (bindings.empty()) {
        ImGui::TextDisabled("Not bound");
    }

    // After the loop, so the list is not changed while it is drawn.
    if (remove) {
        editor.status = "Unbound " + bindingLabel(app, bindings[*remove]) + " from " +
                        kActions[action].label;
        bindings.erase(bindings.begin() + static_cast<long>(*remove));
    }

    if (flip) {
        bindings[*flip].scale = -bindings[*flip].scale;
    }
}

void drawGroup(
    BindingsEditor& editor, Profile& profile, const App& app, const Controls& controls,
    size_t first) {
    const char* group = kActions[first].group;

    if (!ImGui::CollapsingHeader(group, ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;

    if (!ImGui::BeginTable(group, 4, flags)) {
        return;
    }

    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 150.0f);
    ImGui::TableSetupColumn("Activity", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("##bind", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("Bindings", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    for (size_t i = first; i < kActionCount && std::strcmp(kActions[i].group, group) == 0; ++i) {
        const Action action = static_cast<Action>(i);

        ImGui::TableNextRow();
        ImGui::PushID(static_cast<int>(i));

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(kActions[i].label);

        ImGui::TableNextColumn();
        ui::bipolarBar(controls.value(action));

        ImGui::TableNextColumn();

        if (ImGui::Button("Bind", ImVec2(-FLT_MIN, 0.0f))) {
            startCapture(editor, action);
        }

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        drawChips(editor, profile, app, i);

        ImGui::PopID();
    }

    ImGui::EndTable();
}

void drawCapturePopup(BindingsEditor& editor, Profile& profile, const App& app) {
    if (editor.capturing && !ImGui::IsPopupOpen("Bind input")) {
        ImGui::OpenPopup("Bind input");
    }

    if (!ImGui::BeginPopupModal("Bind input", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    // The popup can outlive the capture for a frame after it closes.
    if (!editor.capturing) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    const size_t action = static_cast<size_t>(*editor.capturing);
    ImGui::Text("Binding %s", kActions[action].label);
    ImGui::Separator();

    if (!editor.armed) {
        ImGui::TextDisabled("Release everything to begin.");

        if (!scanPressed(app)) {
            editor.armed = true;
            recordRest(editor, app);
        }
    } else {
        ImGui::TextUnformatted("Press a button or move an axis on a controller.");

        if (kActions[action].directional) {
            ImGui::TextDisabled("Push an axis the way the action should go.");
        }

        std::optional<Binding> binding = scanPressed(app);

        if (!binding) {
            binding = scanRawAxes(editor, app);
        }

        if (binding) {
            editor.status = "Bound " + bindingLabel(app, *binding) + " to " +
                            kActions[action].label;
            profile.bindings[action].push_back(*binding);
            editor.capturing.reset();
            editor.releasing = true;
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::Spacing();

    if (ImGui::Button("Cancel") || app.keyPressed[SDL_SCANCODE_ESCAPE]) {
        editor.capturing.reset();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

// What a new profile starts with, in the order of the New popup's choices.
enum NewFrom { kFromGamepad, kFromEmpty, kFromCurrent };

const char* displayName(const Profile& profile) {
    return profile.name.empty() ? "None" : profile.name.c_str();
}

void switchTo(BindingsEditor& editor, Profiles& profiles, const std::string& name) {
    if (name.empty()) {
        useNoProfile(profiles);
    } else if (!loadProfile(profiles, name)) {
        editor.status = "Could not read " + name;
        return;
    }

    editor.status = std::string("Loaded ") + displayName(profiles.current);
    editor.releasing = true;
}

void openNamePopup(BindingsEditor& editor, const char* popup, const std::string& name) {
    std::snprintf(editor.name, sizeof(editor.name), "%s", name.c_str());
    editor.nameProblem.clear();
    ImGui::OpenPopup(popup);
}

// The name field shared by New and Rename. Returns true on Enter.
bool nameField(BindingsEditor& editor) {
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }

    ImGui::SetNextItemWidth(260.0f);
    const bool entered = ImGui::InputText(
        "##name", editor.name, sizeof(editor.name), ImGuiInputTextFlags_EnterReturnsTrue);

    if (!editor.nameProblem.empty()) {
        ImGui::TextColored(ui::kBad, "%s", editor.nameProblem.c_str());
    }

    return entered;
}

void drawNewPopup(BindingsEditor& editor, Profiles& profiles) {
    if (!ImGui::BeginPopupModal("New profile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    bool create = nameField(editor);

    ImGui::RadioButton("Gamepad layout", &editor.newFrom, kFromGamepad);
    ImGui::RadioButton("No bindings", &editor.newFrom, kFromEmpty);
    ImGui::RadioButton("These bindings", &editor.newFrom, kFromCurrent);
    ImGui::Spacing();

    create |= ImGui::Button("Create");
    ImGui::SameLine();

    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }

    if (create) {
        const std::string name = editor.name;
        editor.nameProblem = profileNameProblem(profiles, name);

        if (editor.nameProblem.empty()) {
            Profile profile = editor.newFrom == kFromGamepad ? gamepadProfile()
                              : editor.newFrom == kFromEmpty ? Profile{}
                                                             : profiles.current;
            profile.name = name;
            profiles.current = profile;

            editor.status = saveProfile(profiles) ? "Created " + name
                                                  : "Could not write " + name;
            editor.releasing = true;
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::EndPopup();
}

void drawRenamePopup(BindingsEditor& editor, Profiles& profiles) {
    if (!ImGui::BeginPopupModal("Rename profile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    bool rename = nameField(editor);
    ImGui::Spacing();

    rename |= ImGui::Button("Rename");
    ImGui::SameLine();

    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }

    if (rename) {
        const std::string name = editor.name;
        editor.nameProblem = profileNameProblem(profiles, name, profiles.current.name);

        if (editor.nameProblem.empty()) {
            if (name != profiles.current.name) {
                editor.status = renameProfile(profiles, name) ? "Renamed to " + name
                                                              : "Could not rename";
            }

            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::EndPopup();
}

void drawDeletePopup(BindingsEditor& editor, Profiles& profiles) {
    if (!ImGui::BeginPopupModal("Delete profile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::Text("Delete %s? Its file is removed and cannot be recovered.",
                profiles.current.name.c_str());
    ImGui::Spacing();

    if (ImGui::Button("Delete")) {
        const std::string name = profiles.current.name;
        editor.status = deleteProfile(profiles) ? "Deleted " + name
                                                : "Could not delete " + name;
        editor.releasing = true;
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();

    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void drawProfileBar(BindingsEditor& editor, Profiles& profiles) {
    const bool named = !profiles.current.name.empty();

    ui::fieldLabel("Profile", 80.0f);
    ImGui::SetNextItemWidth(200.0f);

    if (ImGui::BeginCombo("##profile", displayName(profiles.current))) {
        if (ImGui::Selectable("None", !named)) {
            requestProfileSwitch(editor, profiles, "");
        }

        for (const std::string& name : profiles.names) {
            if (ImGui::Selectable(name.c_str(), name == profiles.current.name)) {
                requestProfileSwitch(editor, profiles, name);
            }
        }

        ImGui::EndCombo();
    }

    ImGui::SameLine();

    if (named) {
        ImGui::BeginDisabled(!profiles.unsaved());

        if (ImGui::Button("Save")) {
            editor.status = saveProfile(profiles) ? "Saved " + profiles.current.name
                                                  : "Could not write " + profiles.current.name;
        }

        ImGui::EndDisabled();
    } else if (ImGui::Button("Save as...")) {
        editor.newFrom = kFromCurrent;
        openNamePopup(editor, "New profile", "");
    }

    ImGui::SameLine();

    if (ImGui::Button("New...")) {
        editor.newFrom = kFromGamepad;
        openNamePopup(editor, "New profile", "");
    }

    if (named) {
        ImGui::SameLine();

        if (ImGui::Button("Rename...")) {
            openNamePopup(editor, "Rename profile", profiles.current.name);
        }

        ImGui::SameLine();

        if (ImGui::Button("Delete...")) {
            ImGui::OpenPopup("Delete profile");
        }
    }

    if (profiles.unsaved()) {
        ImGui::SameLine();
        ImGui::TextColored(ui::kWarn, "Unsaved changes");
    }

    drawNewPopup(editor, profiles);
    drawRenamePopup(editor, profiles);
    drawDeletePopup(editor, profiles);
}

}  // namespace

void requestProfileSwitch(BindingsEditor& editor, Profiles& profiles, const std::string& name) {
    if (name == profiles.current.name) {
        return;
    }

    if (profiles.unsaved()) {
        editor.pendingSwitch = name;
    } else {
        switchTo(editor, profiles, name);
    }
}

void drawProfileSwitchPopup(BindingsEditor& editor, Profiles& profiles) {
    if (editor.pendingSwitch && !ImGui::IsPopupOpen("Unsaved changes")) {
        ImGui::OpenPopup("Unsaved changes");
    }

    if (!ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::Text("Discard the unsaved changes to %s?", displayName(profiles.current));
    ImGui::Spacing();

    if (ImGui::Button("Discard and switch")) {
        switchTo(editor, profiles, editor.pendingSwitch.value_or(""));
        editor.pendingSwitch.reset();
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();

    if (ImGui::Button("Cancel")) {
        editor.pendingSwitch.reset();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

bool holdsControls(BindingsEditor& editor, const App& app) {
    if (editor.releasing && !scanPressed(app)) {
        editor.releasing = false;
    }

    return editor.capturing || editor.releasing;
}

void drawBindingsEditor(
    BindingsEditor& editor, Profiles& profiles, const App& app, const Controls& controls) {
    drawProfileBar(editor, profiles);

    Profile& profile = profiles.current;

    ui::fieldLabel("Deadzone", 80.0f);
    ImGui::SetNextItemWidth(200.0f);
    ImGui::SliderFloat("##deadzone", &profile.deadzone, 0.0f, 0.9f, "%.2f");

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Travel around a stick's centre that reads as zero.");
    }

    ImGui::SameLine();

    if (ImGui::Button("Reset to gamepad layout")) {
        const std::string name = profile.name;
        profile = gamepadProfile();
        profile.name = name;
        editor.status = "Reset to the gamepad layout";
    }

    ImGui::SameLine();
    ImGui::TextDisabled("%s", editor.status.c_str());

    if (profile.name.empty()) {
        ImGui::TextDisabled("None is never saved. Use Save as to keep these bindings.");
    }

    ImGui::Spacing();

    for (size_t i = 0; i < kActionCount; ++i) {
        if (i == 0 || std::strcmp(kActions[i].group, kActions[i - 1].group) != 0) {
            drawGroup(editor, profile, app, controls, i);
        }
    }

    drawCapturePopup(editor, profile, app);
}
