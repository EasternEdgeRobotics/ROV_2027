#include "Pilot.hpp"

#include <eer/app/Ui.hpp>

#include "imgui.h"

#include <algorithm>

namespace {

// Steps per press, the same as 2026: 15 servo steps and 5 LED steps across
// 0..255.
constexpr int kServoStep = 17;
constexpr int kLedStep = 51;

constexpr Action kServoActions[] = {
    Action::Servo1, Action::Servo2, Action::Servo3, Action::Servo4};
constexpr Action kLedActions[] = {Action::Led1, Action::Led2};

float scaled(float value, int power, int axis) {
    return value * power / 100.0f * axis / 100.0f;
}

void step(int& value, const Controls& controls, Action action, int amount) {
    if (controls.pressed(action)) {
        const int direction = controls.value(action) > 0.0f ? 1 : -1;
        value = std::clamp(value + direction * amount, 0, 255);
    }
}

void powerSliders(Power& power) {
    int* values[] = {
        &power.power, &power.surge, &power.sway,
        &power.heave, &power.roll, &power.yaw};
    const char* labels[] = {"Power", "Surge", "Sway", "Heave", "Roll", "Yaw"};

    // Drawn twice in one window, normal and fast mode, so each set needs its
    // own ID scope or the sliders clash.
    ImGui::PushID(&power);

    for (int i = 0; i < 6; ++i) {
        ImGui::PushID(i);
        ui::fieldLabel(labels[i], 70.0f);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderInt("##value", values[i], 0, 100, "%d%%");
        ImGui::PopID();
    }

    ImGui::PopID();
}

}  // namespace

void updatePilot(Pilot& pilot, const Controls& controls) {
    if (controls.pressed(Action::FastModeToggle)) {
        pilot.fastToggled = !pilot.fastToggled;
    }

    if (controls.pressed(Action::InvertControls)) {
        pilot.invert = !pilot.invert;
    }

    pilot.fastMode = pilot.fastToggled || controls.held(Action::FastModeHold);

    const Power& power = activePower(pilot);
    PilotOutput& out = pilot.out;

    // Same as 2026, which flipped surge and yaw only.
    const float flip = pilot.invert ? -1.0f : 1.0f;

    out.surge = scaled(controls.value(Action::Surge) * flip, power.power, power.surge);
    out.sway = scaled(controls.value(Action::Sway), power.power, power.sway);
    out.heave = scaled(controls.value(Action::Heave), power.power, power.heave);
    out.yaw = scaled(controls.value(Action::Yaw) * flip, power.power, power.yaw);
    out.roll = scaled(controls.value(Action::Roll), power.power, power.roll);

    for (int i = 0; i < 4; ++i) {
        step(out.servo[i], controls, kServoActions[i], kServoStep);
    }

    for (int i = 0; i < 2; ++i) {
        step(out.led[i], controls, kLedActions[i], kLedStep);
    }

    out.motor[0] = controls.value(Action::Motor1);
    out.motor[1] = controls.value(Action::Motor2);
}

const Power& activePower(const Pilot& pilot) {
    return pilot.fastMode ? pilot.fastPower : pilot.power;
}

void drawCoPilotWindow(Pilot& pilot, bool* open) {
    ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Co-Pilot", open)) {
        ImGui::End();
        return;
    }

    if (pilot.fastMode) {
        ImGui::TextColored(ui::kWarn, "Fast mode on, using the fast mode power");
    } else {
        ImGui::TextDisabled("Fast mode off");
    }

    if (pilot.invert) {
        ImGui::TextColored(ui::kWarn, "Controls inverted");
    }

    ImGui::SeparatorText("Power");
    powerSliders(pilot.power);

    ImGui::Spacing();

    for (int i = 0; i < static_cast<int>(std::size(kPowerPresets)); ++i) {
        ImGui::PushID(i);

        if (ImGui::Button(kPowerPresets[i].label)) {
            pilot.power = kPowerPresets[i].power;
        }

        ImGui::PopID();

        if (i % 2 == 0) {
            ImGui::SameLine();
        }
    }

    ImGui::SeparatorText("Fast mode power");

    ImGui::Checkbox("Fast mode", &pilot.fastToggled);
    powerSliders(pilot.fastPower);

    ImGui::End();
}
