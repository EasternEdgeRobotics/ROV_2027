#pragma once

#include <filesystem>
#include <string>

// Where an app keeps its files. Every app shares one directory, so file names
// carry the app's name.
namespace paths {

// macOS    ~/Library/Application Support/Eastern Edge
// Windows  %APPDATA%\Eastern Edge
// else     $XDG_CONFIG_HOME/eastern-edge, which is ~/.config unless moved
//
// EER_CONFIG_DIR overrides all three. Falls back to the working directory
// only when the OS cannot name a home.
std::filesystem::path configDir();

// <configDir>/<appName>_config.json
std::filesystem::path configFile(const std::string& appName);

// <configDir>/<appName>_imgui.ini, creating the directory if needed. ImGui
// keeps the pointer instead of copying it, so the string is held here for the
// life of the process. Call once per process.
const char* imguiIniPath(const std::string& appName);

// The user's Pictures folder, or the best guess when the OS does not say.
std::filesystem::path picturesDir();

// <picturesDir>/<folder>/<local time>, for example
// Pictures/eer_captures/2027-06-20_14-05-09. Not created here.
std::filesystem::path sessionDir(const std::string& folder);

}  // namespace paths
