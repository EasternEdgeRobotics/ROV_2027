#pragma once

#include "imgui.h"

// Small widgets every app draws the same way.
namespace ui {

constexpr ImVec4 kGood{0.35f, 0.80f, 0.45f, 1.0f};
constexpr ImVec4 kWarn{0.90f, 0.65f, 0.30f, 1.0f};
constexpr ImVec4 kBad{0.88f, 0.36f, 0.36f, 1.0f};
constexpr ImVec4 kInfo{0.55f, 0.75f, 1.00f, 1.0f};
constexpr ImVec4 kIdle{0.60f, 0.62f, 0.66f, 1.0f};

// Moves the cursor so the next `width` pixels of items end at the right edge.
// ImGui has no native right alignment, so the caller has to measure first.
void alignRight(float width);

// Two column key/value block, dimmed key on the left. Used for About pages.
// Only call infoRow and endInfoTable when beginInfoTable returned true.
bool beginInfoTable(const char* id);
void infoRow(const char* key, const char* value);
void endInfoTable();

// ImGui puts a widget's label on its right, which reads badly in a form. This
// draws it on the left and leaves the cursor a fixed distance past where the
// label began, so a column of fields lines up.
void fieldLabel(const char* text, float labelWidth = 110.0f);

// A bar for a -1..1 value that fills from the centre, left for negative and
// right for positive, with the value written on it. Fills the available width.
void bipolarBar(float value);

}  // namespace ui
