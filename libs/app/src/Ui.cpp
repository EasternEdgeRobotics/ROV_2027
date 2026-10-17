#include <eer/app/Ui.hpp>

#include <algorithm>
#include <cstdio>

namespace ui {
void alignRight(float width) {
    const float avail = ImGui::GetContentRegionAvail().x;

    if (avail > width) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - width);
    }
}

bool beginInfoTable(const char* id) {
    return ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit);
}

void infoRow(const char* key, const char* value) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", key);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(value);
}

void endInfoTable() {
    ImGui::EndTable();
}

void fieldLabel(const char* text, float labelWidth) {
    const float x = ImGui::GetCursorPosX();

    // So the label sits on the widget's baseline and not against its top.
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    ImGui::SameLine();
    ImGui::SetCursorPosX(x + labelWidth);
}

void bipolarBar(float value) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
    ImGui::Dummy(size);

    const float mid = pos.x + size.x / 2.0f;
    const float end = mid + std::clamp(value, -1.0f, 1.0f) * size.x / 2.0f;
    const float bottom = pos.y + size.y;

    draw->AddRectFilled(
        pos, ImVec2(pos.x + size.x, bottom), ImGui::GetColorU32(ImGuiCol_FrameBg),
        ImGui::GetStyle().FrameRounding);
    draw->AddRectFilled(
        ImVec2(std::min(mid, end), pos.y), ImVec2(std::max(mid, end), bottom),
        ImGui::GetColorU32(ImGuiCol_PlotHistogram));
    draw->AddLine(
        ImVec2(mid, pos.y), ImVec2(mid, bottom), ImGui::GetColorU32(ImGuiCol_Border));

    char text[16];
    std::snprintf(text, sizeof(text), "%+.2f", value);
    const ImVec2 textSize = ImGui::CalcTextSize(text);

    draw->AddText(
        ImVec2(mid - textSize.x / 2.0f, pos.y + (size.y - textSize.y) / 2.0f),
        ImGui::GetColorU32(ImGuiCol_Text), text);
}

}  // namespace ui
