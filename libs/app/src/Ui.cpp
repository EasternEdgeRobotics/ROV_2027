#include <eer/app/Ui.hpp>

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

}  // namespace ui
