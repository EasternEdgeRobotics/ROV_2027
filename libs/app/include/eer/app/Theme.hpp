#pragma once

// The look shared by every app. Colours, spacing, rounding and the font size
// are all in the block at the top of src/Theme.cpp.
//
// Call once after ImGui::CreateContext(). appInit() already does.
void applyTheme();

// Live colour editor. Call once per frame while `*open` is true.
//
// Colour edits apply instantly. To keep them, use "Copy values to clipboard"
// and paste over the block in src/Theme.cpp. The font size is baked into a
// texture at load, so changing that needs a rebuild.
void drawThemeEditor(bool* open);
