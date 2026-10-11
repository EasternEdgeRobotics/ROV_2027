#include <eer/app/Theme.hpp>

#include "imgui.h"

#if defined(__APPLE__)
// For the system UI font. CoreText is a C API, so this stays plain C++.
#include <CoreText/CoreText.h>
#include <climits>
#endif

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>

// ---------------------------------------------------------------------------
// TWEAK ME
//
// Everything below this block is plumbing that maps these values onto ImGui's
// style colours. Change numbers here and rebuild, or press F10 in an app to
// edit them live and paste the result back.
// ---------------------------------------------------------------------------
namespace theme {

// Colours are 0xRRGGBB. The blue is Eastern Edge's, 0x007AD9, sampled from
// the no signal image. The darker steps are turned about 8 degrees towards
// pure blue, the way the dark no signal image's navy (0x002050) is. A blue
// darkened at the same numeric hue looks teal beside the bright one.
constexpr unsigned kBgWindow    = 0x14161B; // outermost background
constexpr unsigned kBgPanel     = 0x1B1E25; // child windows, popups, menus
constexpr unsigned kBgFrame     = 0x232732; // inputs, buttons, sliders at rest
constexpr unsigned kBgFrameHot  = 0x1F2C4A; // hovered, a blue tinted lift
constexpr unsigned kBgFrameOn   = 0x003A85; // held or active

// Also drives separators and the table grid, so keep it quiet. A saturated
// colour here leaves nothing for the accent to stand out against.
constexpr unsigned kBorder      = 0x242E42;

// Chrome: title bars, the menu bar and table header rows.
constexpr unsigned kChrome      = 0x001640; // at rest, and unfocused windows
constexpr unsigned kChromeOn    = 0x003A85; // the focused window's title bar

constexpr unsigned kText        = 0xE6E9EF;
constexpr unsigned kTextDim     = 0x98A1B0; // disabled text, hints

// One hue, three lightnesses, so interaction reads as intensity and not as a
// colour change next to the status colours below.
constexpr unsigned kAccent      = 0x007AD9; // selection, checks
constexpr unsigned kAccentHot   = 0x3D9FEA;
constexpr unsigned kAccentDim   = 0x004FB0;

constexpr unsigned kWarning     = 0xE5A54B;
constexpr unsigned kDanger      = 0xE05C5C;

// Set to 0 to keep ImGui's built in font. Fonts bake into a texture at load,
// so unlike the colours this is not live editable.
constexpr float kFontSizePx     = 16.0f;

// Spacing. ImGui's defaults are much tighter than a normal desktop app, and
// most of the designed feeling comes from these, not the colours.
constexpr ImVec2 kWindowPadding = ImVec2(16.0f, 14.0f);
constexpr ImVec2 kFramePadding  = ImVec2(12.0f,  7.0f);
constexpr ImVec2 kItemSpacing   = ImVec2(10.0f,  8.0f);
constexpr ImVec2 kInnerSpacing  = ImVec2( 8.0f,  6.0f);

// Rounding. 0 everywhere is the stock ImGui look.
constexpr float kRoundFrame     = 6.0f;
constexpr float kRoundWindow    = 8.0f;
constexpr float kRoundChild     = 8.0f;
constexpr float kRoundPopup     = 8.0f;
constexpr float kRoundGrab      = 6.0f;
constexpr float kRoundScrollbar = 8.0f;
constexpr float kRoundTab       = 6.0f;

// Borders. ImGui draws 1px lines around nearly everything by default;
// modern UI separates with background tone instead. 0 disables.
constexpr float kBorderWindow   = 0.0f;
constexpr float kBorderFrame    = 0.0f;
constexpr float kBorderChild    = 1.0f;
constexpr float kBorderPopup    = 1.0f;

constexpr float kScrollbarSize  = 12.0f;
constexpr float kGrabMinSize    = 12.0f;

} // namespace theme
// ---------------------------------------------------------------------------
// End of tweakables.
// ---------------------------------------------------------------------------

namespace {

ImVec4 rgb(unsigned hex, float alpha = 1.0f) {
    return ImVec4(
        static_cast<float>((hex >> 16) & 0xFF) / 255.0f,
        static_cast<float>((hex >>  8) & 0xFF) / 255.0f,
        static_cast<float>((hex      ) & 0xFF) / 255.0f,
        alpha);
}

ImVec4 withAlpha(ImVec4 colour, float alpha) {
    colour.w = alpha;
    return colour;
}

unsigned toHex(const ImVec4& colour) {
    auto channel = [](float v) {
        return static_cast<unsigned>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };

    return (channel(colour.x) << 16) | (channel(colour.y) << 8) | channel(colour.z);
}

// Runtime copy of the palette above, which is what the live editor changes.
struct LiveColours {
    ImVec4 bgWindow;
    ImVec4 bgPanel;
    ImVec4 bgFrame;
    ImVec4 bgFrameHot;
    ImVec4 bgFrameOn;
    ImVec4 border;
    ImVec4 chrome;
    ImVec4 chromeOn;
    ImVec4 text;
    ImVec4 textDim;
    ImVec4 accent;
    ImVec4 accentHot;
    ImVec4 accentDim;
    ImVec4 warning;
    ImVec4 danger;
};

LiveColours g_colours;

void seedFromConstants() {
    using namespace theme;

    g_colours.bgWindow   = rgb(kBgWindow);
    g_colours.bgPanel    = rgb(kBgPanel);
    g_colours.bgFrame    = rgb(kBgFrame);
    g_colours.bgFrameHot = rgb(kBgFrameHot);
    g_colours.bgFrameOn  = rgb(kBgFrameOn);
    g_colours.border     = rgb(kBorder);
    g_colours.chrome     = rgb(kChrome);
    g_colours.chromeOn   = rgb(kChromeOn);
    g_colours.text       = rgb(kText);
    g_colours.textDim    = rgb(kTextDim);
    g_colours.accent     = rgb(kAccent);
    g_colours.accentHot  = rgb(kAccentHot);
    g_colours.accentDim  = rgb(kAccentDim);
    g_colours.warning    = rgb(kWarning);
    g_colours.danger     = rgb(kDanger);
}

void applyColours(const LiveColours& t) {
    ImVec4* c = ImGui::GetStyle().Colors;

    c[ImGuiCol_Text]                  = t.text;
    c[ImGuiCol_TextDisabled]          = t.textDim;

    c[ImGuiCol_WindowBg]              = t.bgWindow;
    c[ImGuiCol_ChildBg]               = t.bgPanel;
    c[ImGuiCol_PopupBg]               = t.bgPanel;
    c[ImGuiCol_MenuBarBg]             = t.chrome;

    c[ImGuiCol_Border]                = t.border;
    c[ImGuiCol_BorderShadow]          = rgb(0x000000, 0.0f);

    c[ImGuiCol_FrameBg]               = t.bgFrame;
    c[ImGuiCol_FrameBgHovered]        = t.bgFrameHot;
    c[ImGuiCol_FrameBgActive]         = t.bgFrameOn;

    // Active is the brighter one, so it is clear which window has focus.
    c[ImGuiCol_TitleBg]               = t.chrome;
    c[ImGuiCol_TitleBgActive]         = t.chromeOn;
    c[ImGuiCol_TitleBgCollapsed]      = withAlpha(t.chrome, 0.75f);

    c[ImGuiCol_ScrollbarBg]           = withAlpha(t.bgWindow, 0.0f);
    c[ImGuiCol_ScrollbarGrab]         = t.bgFrameHot;
    c[ImGuiCol_ScrollbarGrabHovered]  = t.bgFrameOn;
    c[ImGuiCol_ScrollbarGrabActive]   = t.accentDim;

    c[ImGuiCol_CheckMark]             = t.accent;
    c[ImGuiCol_SliderGrab]            = t.accentDim;
    c[ImGuiCol_SliderGrabActive]      = t.accent;

    c[ImGuiCol_Button]                = t.bgFrame;
    c[ImGuiCol_ButtonHovered]         = t.bgFrameHot;
    c[ImGuiCol_ButtonActive]          = t.bgFrameOn;

    c[ImGuiCol_Header]                = withAlpha(t.accentDim, 0.35f);
    c[ImGuiCol_HeaderHovered]         = withAlpha(t.accentDim, 0.55f);
    c[ImGuiCol_HeaderActive]          = withAlpha(t.accentDim, 0.75f);

    c[ImGuiCol_Separator]             = t.border;
    c[ImGuiCol_SeparatorHovered]      = t.accentDim;
    c[ImGuiCol_SeparatorActive]       = t.accent;

    c[ImGuiCol_ResizeGrip]            = withAlpha(t.bgFrameHot, 0.6f);
    c[ImGuiCol_ResizeGripHovered]     = t.accentDim;
    c[ImGuiCol_ResizeGripActive]      = t.accent;

    c[ImGuiCol_Tab]                   = t.bgPanel;
    c[ImGuiCol_TabHovered]            = t.bgFrameHot;
    c[ImGuiCol_TabSelected]           = t.bgFrameOn;
    c[ImGuiCol_TabSelectedOverline]   = t.accent;
    c[ImGuiCol_TabDimmed]             = t.bgPanel;
    c[ImGuiCol_TabDimmedSelected]     = t.bgFrame;

    c[ImGuiCol_PlotLines]             = t.accent;
    c[ImGuiCol_PlotLinesHovered]      = t.accentHot;
    c[ImGuiCol_PlotHistogram]         = t.warning;
    c[ImGuiCol_PlotHistogramHovered]  = t.danger;

    c[ImGuiCol_TableHeaderBg]         = t.chrome;
    c[ImGuiCol_TableBorderStrong]     = t.border;
    c[ImGuiCol_TableBorderLight]      = withAlpha(t.border, 0.5f);
    c[ImGuiCol_TableRowBg]            = rgb(0x000000, 0.0f);
    c[ImGuiCol_TableRowBgAlt]         = rgb(0xFFFFFF, 0.02f);

    c[ImGuiCol_TextSelectedBg]        = withAlpha(t.accent, 0.35f);
    c[ImGuiCol_DragDropTarget]        = t.accent;
    c[ImGuiCol_NavCursor]             = t.accent;
    c[ImGuiCol_NavWindowingHighlight] = withAlpha(t.accent, 0.7f);
    c[ImGuiCol_NavWindowingDimBg]     = rgb(0x000000, 0.5f);
    c[ImGuiCol_ModalWindowDimBg]      = rgb(0x000000, 0.6f);
}

void applyMetrics() {
    using namespace theme;

    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowPadding     = kWindowPadding;
    s.FramePadding      = kFramePadding;
    s.ItemSpacing       = kItemSpacing;
    s.ItemInnerSpacing  = kInnerSpacing;
    s.CellPadding       = kInnerSpacing;

    s.WindowRounding    = kRoundWindow;
    s.ChildRounding     = kRoundChild;
    s.FrameRounding     = kRoundFrame;
    s.PopupRounding     = kRoundPopup;
    s.GrabRounding      = kRoundGrab;
    s.ScrollbarRounding = kRoundScrollbar;
    s.TabRounding       = kRoundTab;

    s.WindowBorderSize  = kBorderWindow;
    s.FrameBorderSize   = kBorderFrame;
    s.ChildBorderSize   = kBorderChild;
    s.PopupBorderSize   = kBorderPopup;

    s.ScrollbarSize     = kScrollbarSize;
    s.GrabMinSize       = kGrabMinSize;

    s.WindowTitleAlign  = ImVec2(0.0f, 0.5f);
}

#if defined(__APPLE__)

// Where the OS keeps its own UI font. Asked through CoreText because the
// path differs between macOS and iOS and is not stable across releases.
// Empty on failure.
std::string systemUiFontPath() {
    CTFontRef font =
        CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, 0.0, nullptr);

    if (!font) {
        return {};
    }

    CFURLRef url = static_cast<CFURLRef>(
        CTFontCopyAttribute(font, kCTFontURLAttribute));
    CFRelease(font);

    if (!url) {
        return {};
    }

    char path[PATH_MAX] = {};
    const bool ok = CFURLGetFileSystemRepresentation(
        url, true, reinterpret_cast<UInt8*>(path), sizeof path);
    CFRelease(url);

    return ok ? std::string(path) : std::string();
}

#endif  // __APPLE__

void loadUiFont() {
    if (theme::kFontSizePx <= 0.0f) {
        return;
    }

#if defined(__APPLE__)
    if (const std::string systemFont = systemUiFontPath(); !systemFont.empty()) {
        if (ImGui::GetIO().Fonts->AddFontFromFileTTF(systemFont.c_str(),
                                                     theme::kFontSizePx)) {
            return;
        }

        std::cerr << "Theme: could not load the system font at " << systemFont
                  << ", falling back" << std::endl;
    }
#endif

    const char* candidates[] = {
#if defined(__APPLE__)
        // Only reached if CoreText failed.
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/SFNSDisplay.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Supplemental/Comic Sans MS Bold.ttf",
        "/System/Library/Fonts/Supplemental/Comic Sans MS.ttf",
#elif defined(_WIN32)
        // Semibold because we cant expect a high dpi display, and with fractional
        // scaling it loows AWFUL otherwise.
        "C:\\Windows\\Fonts\\seguisb.ttf",
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
#else
        // Includes paths for Fedora, Ubuntu and Alpine, hopefully that
        // casts a wide enough net to get whatever we would run this on
        "/usr/share/fonts/adwaita-mono-fonts/AdwaitaMono-Regular.ttf",
        "/usr/share/fonts/open-sans/OpenSans-Regular.ttf",
        "/usr/share/fonts/google-droid-sans-fonts/DroidSans.ttf",
        "/usr/share/fonts/google-noto/NotoSansMath-Regular.ttf",
        "/usr/share/fonts/google-noto-vf/NotoSans[wght].ttf",
        "/usr/share/fonts/truetype/inter/Inter-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/roboto/Roboto-Medium.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
#endif
    };

    ImFontConfig cfg;

#if defined(_WIN32)
    // Same washed out text problem as the face choice above. ImGui documents
    // this field as the workaround.
    cfg.RasterizerMultiply = 1.3f;
#endif

    for (const char* path : candidates) {
        if (!std::filesystem::exists(path)) {
            continue;
        }

        if (ImGui::GetIO().Fonts->AddFontFromFileTTF(
                path, theme::kFontSizePx, &cfg)) {
            return;
        }
    }

    std::cerr << "Theme: no system UI font found, using ImGui default"
              << std::endl;
}


// One row of the live editor.
void colourRow(const char* label, const char* comment, ImVec4& target) {
    ImGui::ColorEdit3(
        label,
        &target.x,
        ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_NoAlpha);

    if (comment && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", comment);
    }
}

std::string exportConstants() {
    struct Row { const char* name; const ImVec4* colour; };

    const Row rows[] = {
        { "kBgWindow   ", &g_colours.bgWindow   },
        { "kBgPanel    ", &g_colours.bgPanel    },
        { "kBgFrame    ", &g_colours.bgFrame    },
        { "kBgFrameHot ", &g_colours.bgFrameHot },
        { "kBgFrameOn  ", &g_colours.bgFrameOn  },
        { "kBorder     ", &g_colours.border     },
        { "kChrome     ", &g_colours.chrome     },
        { "kChromeOn   ", &g_colours.chromeOn   },
        { "kText       ", &g_colours.text       },
        { "kTextDim    ", &g_colours.textDim    },
        { "kAccent     ", &g_colours.accent     },
        { "kAccentHot  ", &g_colours.accentHot  },
        { "kAccentDim  ", &g_colours.accentDim  },
        { "kWarning    ", &g_colours.warning    },
        { "kDanger     ", &g_colours.danger     },
    };

    std::string out;
    char line[128];

    for (const Row& row : rows) {
        std::snprintf(
            line,
            sizeof(line),
            "constexpr unsigned %s = 0x%06X;\n",
            row.name,
            toHex(*row.colour));

        out += line;
    }

    return out;
}


} // namespace

void applyTheme() {
    loadUiFont();
    seedFromConstants();
    applyMetrics();
    applyColours(g_colours);
}


void drawThemeEditor(bool* open) {
    ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Theme", open)) {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Edits apply instantly. Paste back to persist.");
    ImGui::Spacing();

    colourRow("Window bg",   "Outermost background",            g_colours.bgWindow);
    colourRow("Panel bg",    "Child windows, popups, menus",    g_colours.bgPanel);
    colourRow("Frame",       "Inputs and buttons at rest",      g_colours.bgFrame);
    colourRow("Frame hover", "Inputs and buttons hovered",      g_colours.bgFrameHot);
    colourRow("Frame active","Inputs and buttons held",         g_colours.bgFrameOn);
    colourRow("Border",      "Separators, child window edges",  g_colours.border);

    ImGui::Spacing();

    colourRow("Chrome",      "Menu bar, table headers, title bars", g_colours.chrome);
    colourRow("Chrome focus","Focused window's title bar",      g_colours.chromeOn);

    ImGui::Spacing();

    colourRow("Text",        "Primary text",                    g_colours.text);
    colourRow("Text dim",    "Disabled text and hints",         g_colours.textDim);

    ImGui::Spacing();

    colourRow("Accent",      "Checks, selection, active tab",   g_colours.accent);
    colourRow("Accent hot",  "Accent, hovered",                 g_colours.accentHot);
    colourRow("Accent dim",  "Accent, recessed (headers)",      g_colours.accentDim);

    ImGui::Spacing();

    colourRow("Warning",     "Currently only plot histograms",  g_colours.warning);
    colourRow("Danger",      "Currently only plot histograms",  g_colours.danger);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Copy values to clipboard")) {
        ImGui::SetClipboardText(exportConstants().c_str());
    }

    ImGui::SameLine();

    if (ImGui::Button("Reset")) {
        seedFromConstants();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Paste into the TWEAK ME block in libs/app/src/Theme.cpp.");

    // Cheap enough to reapply every frame.
    applyColours(g_colours);

    ImGui::End();
}

