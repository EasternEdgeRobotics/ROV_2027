#include "DummyRov.hpp"

#include <eer/app/Ui.hpp>

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

constexpr float kPi = 3.14159265f;

constexpr float kPoolWidth = 12.0f;
constexpr float kPoolLength = 8.0f;
constexpr float kPoolDepth = 4.0f;

// Full command in metres or radians per second. Rough, not measured.
constexpr float kSurgeSpeed = 1.0f;
constexpr float kSwaySpeed = 0.7f;
constexpr float kHeaveSpeed = 0.5f;
constexpr float kYawSpeed = kPi / 2.0f;

// How quickly a speed catches up with its command, per second.
constexpr float kResponse = 3.0f;

void approach(float& speed, float target, float dt) {
    speed += (target - speed) * std::min(1.0f, dt * kResponse);
}

void simulate(DummyRov& rov, const PilotOutput& out, float dt) {
    approach(rov.surgeSpeed, out.surge * kSurgeSpeed, dt);
    approach(rov.swaySpeed, out.sway * kSwaySpeed, dt);
    approach(rov.heaveSpeed, out.heave * kHeaveSpeed, dt);
    approach(rov.yawSpeed, out.yaw * kYawSpeed, dt);

    rov.heading = std::fmod(rov.heading + rov.yawSpeed * dt, 2.0f * kPi);

    const float forwardX = std::sin(rov.heading);
    const float forwardY = std::cos(rov.heading);

    // Right of forward, turned a quarter clockwise.
    rov.x += (forwardX * rov.surgeSpeed + forwardY * rov.swaySpeed) * dt;
    rov.y += (forwardY * rov.surgeSpeed - forwardX * rov.swaySpeed) * dt;

    rov.x = std::clamp(rov.x, -kPoolWidth / 2.0f, kPoolWidth / 2.0f);
    rov.y = std::clamp(rov.y, -kPoolLength / 2.0f, kPoolLength / 2.0f);
    rov.depth = std::clamp(rov.depth - rov.heaveSpeed * dt, 0.0f, kPoolDepth);
}

void drawPool(const DummyRov& rov, ImVec2 size) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);

    const float depthBarWidth = 24.0f;
    const float gap = 8.0f;
    const float poolSpace = size.x - depthBarWidth - gap;
    const float scale = std::min(poolSpace / kPoolWidth, size.y / kPoolLength);

    const float left = origin.x + (poolSpace - kPoolWidth * scale) / 2.0f;
    const float top = origin.y + (size.y - kPoolLength * scale) / 2.0f;
    const float centreX = left + kPoolWidth * scale / 2.0f;
    const float centreY = top + kPoolLength * scale / 2.0f;

    const ImU32 water = ImGui::GetColorU32(ImGuiCol_FrameBg);
    const ImU32 edge = ImGui::GetColorU32(ImGuiCol_Border);
    const ImU32 body = ImGui::GetColorU32(ui::kInfo);

    draw->AddRectFilled(
        ImVec2(left, top),
        ImVec2(left + kPoolWidth * scale, top + kPoolLength * scale), water);
    draw->AddRect(
        ImVec2(left, top),
        ImVec2(left + kPoolWidth * scale, top + kPoolLength * scale), edge);

    const float fx = std::sin(rov.heading);
    const float fy = std::cos(rov.heading);

    // Screen y runs down, so every y is negated.
    auto point = [&](float forward, float right) {
        return ImVec2(
            centreX + (rov.x + fx * forward + fy * right) * scale,
            centreY - (rov.y + fy * forward - fx * right) * scale);
    };

    draw->AddTriangleFilled(
        point(0.4f, 0.0f), point(-0.25f, 0.25f), point(-0.25f, -0.25f), body);

    const float barLeft = origin.x + size.x - depthBarWidth;
    const float level = origin.y + rov.depth / kPoolDepth * size.y;

    draw->AddRectFilled(
        ImVec2(barLeft, origin.y), ImVec2(barLeft + depthBarWidth, origin.y + size.y),
        water);
    draw->AddRect(
        ImVec2(barLeft, origin.y), ImVec2(barLeft + depthBarWidth, origin.y + size.y),
        edge);
    draw->AddLine(
        ImVec2(barLeft, level), ImVec2(barLeft + depthBarWidth, level), body, 3.0f);
}

void byteBar(int value) {
    char text[16];
    std::snprintf(text, sizeof(text), "%d", value);
    ImGui::ProgressBar(value / 255.0f, ImVec2(-FLT_MIN, 0.0f), text);
}

void outputRow(const char* label) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
}

}  // namespace

void drawDummyRov(DummyRov& rov, const PilotOutput& out, bool* open) {
    ImGui::SetNextWindowSize(ImVec2(460, 620), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Dummy ROV", open)) {
        ImGui::End();
        return;
    }

    simulate(rov, out, ImGui::GetIO().DeltaTime);

    ImGui::TextDisabled(
        "Flies on the pilot's output while this window is open. Nothing is sent "
        "to a vehicle.");

    if (ImGui::Button("Reset position")) {
        rov = DummyRov{};
    }

    ImGui::SameLine();
    ImGui::Text(
        "Heading %.0f    Depth %.1f m", rov.heading * 180.0f / kPi, rov.depth);

    const float width = ImGui::GetContentRegionAvail().x;
    drawPool(rov, ImVec2(width, width * kPoolLength / kPoolWidth));

    ImGui::Spacing();

    if (ImGui::BeginTable("outputs", 2, ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Output", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

        const char* axisLabels[] = {"Surge", "Sway", "Heave", "Yaw", "Roll"};
        const float axes[] = {out.surge, out.sway, out.heave, out.yaw, out.roll};

        for (int i = 0; i < 5; ++i) {
            outputRow(axisLabels[i]);
            ui::bipolarBar(axes[i]);
        }

        for (int i = 0; i < 4; ++i) {
            const std::string label = "Servo " + std::to_string(i + 1);
            outputRow(label.c_str());
            byteBar(out.servo[i]);
        }

        for (int i = 0; i < 2; ++i) {
            const std::string label = "LED " + std::to_string(i + 1);
            outputRow(label.c_str());
            byteBar(out.led[i]);
        }

        for (int i = 0; i < 2; ++i) {
            const std::string label = "Motor " + std::to_string(i + 1);
            outputRow(label.c_str());
            ui::bipolarBar(out.motor[i]);
        }

        ImGui::EndTable();
    }

    ImGui::End();
}
