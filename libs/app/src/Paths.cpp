// windows.h comes first because both defines only work if nothing has pulled
// it in already. NOMINMAX stops its min/max macros breaking std::min.
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#endif

#include <eer/app/Paths.hpp>

#include <SDL3/SDL.h>

#include <cstdlib>
#include <system_error>

namespace fs = std::filesystem;

namespace paths {
namespace {

// Empty when the variable is unset or blank.
std::string env(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}

#if defined(_WIN32)
// Returns an fs::path because the string is wide. Narrowing it first would
// mangle a non-ASCII user name.
fs::path roamingAppData() {
    PWSTR raw = nullptr;
    const HRESULT result = SHGetKnownFolderPath(
        FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &raw);

    // Documented to need freeing even on failure.
    fs::path path = SUCCEEDED(result) ? fs::path(raw) : fs::path();
    CoTaskMemFree(raw);
    return path;
}
#endif

}  // namespace

fs::path configDir() {
    if (const std::string dir = env("EER_CONFIG_DIR"); !dir.empty()) {
        return dir;
    }

#if defined(_WIN32)
    const fs::path appData = roamingAppData();
    return appData.empty() ? fs::path(".") : appData / "Eastern Edge";
#else
    const std::string home = env("HOME");

    if (home.empty()) {
        return ".";
    }

#if defined(__APPLE__)
    return fs::path(home) / "Library" / "Application Support" / "Eastern Edge";
#else
    // The XDG spec says a relative XDG_CONFIG_HOME is invalid and must be
    // ignored, hence the leading slash test.
    const std::string xdg = env("XDG_CONFIG_HOME");
    const fs::path base =
        (!xdg.empty() && xdg[0] == '/') ? fs::path(xdg) : fs::path(home) / ".config";

    return base / "eastern-edge";
#endif
#endif
}

fs::path configFile(const std::string& appName) {
    return configDir() / (appName + "_config.json");
}

const char* imguiIniPath(const std::string& appName) {
    static std::string path;

    const fs::path file = configDir() / (appName + "_imgui.ini");

    // ImGui will not create the directory, and will not say that it could not.
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);

    path = file.string();
    return path.c_str();
}

fs::path picturesDir() {
    // SDL returns UTF-8. A plain char string is read as the active code page
    // on Windows, so it has to go through char8_t to survive.
    if (const char* pictures = SDL_GetUserFolder(SDL_FOLDER_PICTURES)) {
        const std::string utf8 = pictures;
        return fs::path(std::u8string(utf8.begin(), utf8.end()));
    }

    // Normal on a minimal Linux install with no xdg-user-dirs.
    const std::string home = env("HOME");
    return home.empty() ? fs::path(".") : fs::path(home) / "Pictures";
}

}  // namespace paths
