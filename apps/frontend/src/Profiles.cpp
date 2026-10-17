#include "Profiles.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <optional>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

constexpr const char* kHatDirections[] = {"up", "right", "down", "left"};

// Gamepad buttons and axes are stored by SDL's names for them, so a profile
// file can be read and fixed by hand. Raw joystick inputs have no names.
struct SourceName {
    Source source;
    const char* name;
};

constexpr SourceName kSourceNames[] = {
    {Source::Button, "button"},
    {Source::Axis, "axis"},
    {Source::JoyButton, "joy_button"},
    {Source::JoyAxis, "joy_axis"},
    {Source::JoyHat, "joy_hat"},
};

bool equalsNoCase(const std::string& a, const std::string& b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) ==
                      std::tolower(static_cast<unsigned char>(y));
           });
}

// Letters, numbers, spaces, - and _ only, which is a valid filename on every
// platform and cannot climb out of the profiles folder.
bool validCharacters(const std::string& name) {
    return std::all_of(name.begin(), name.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' ||
               c == '_';
    });
}

fs::path profilePath(const Profiles& profiles, const std::string& name) {
    return profiles.dir / (name + ".json");
}

std::optional<Binding> readBinding(const json& data) {
    if (!data.is_object() || !data.contains("source") || !data["source"].is_string()) {
        return std::nullopt;
    }

    const std::string sourceName = data["source"].get<std::string>();
    const auto source = std::find_if(
        std::begin(kSourceNames), std::end(kSourceNames),
        [&](const SourceName& entry) { return sourceName == entry.name; });

    if (source == std::end(kSourceNames)) {
        return std::nullopt;
    }

    Binding binding;
    binding.source = source->source;

    if (data.contains("scale") && data["scale"].is_number()) {
        binding.scale = data["scale"].get<float>() < 0.0f ? -1.0f : 1.0f;
    }

    if (data.contains("from_min") && data["from_min"].is_boolean()) {
        binding.fromMin = data["from_min"].get<bool>();
    }

    const json input = data.contains("input") ? data["input"] : json();

    if (binding.source == Source::Button || binding.source == Source::Axis) {
        if (!input.is_string()) {
            return std::nullopt;
        }

        const std::string name = input.get<std::string>();
        if (binding.source == Source::Button) {
            binding.index = SDL_GetGamepadButtonFromString(name.c_str());
        } else {
            binding.index = SDL_GetGamepadAxisFromString(name.c_str());
        }

        // Both INVALID values are -1.
        return binding.index < 0 ? std::nullopt : std::optional(binding);
    }

    if (!data.contains("device") || !data["device"].is_string()) {
        return std::nullopt;
    }

    binding.device = data["device"].get<std::string>();

    if (binding.source == Source::JoyHat) {
        if (!data.contains("hat") || !data["hat"].is_number_integer() ||
            !data.contains("direction") || !data["direction"].is_string()) {
            return std::nullopt;
        }

        const std::string direction = data["direction"].get<std::string>();
        const auto found =
            std::find(std::begin(kHatDirections), std::end(kHatDirections), direction);

        if (found == std::end(kHatDirections) || data["hat"].get<int>() < 0) {
            return std::nullopt;
        }

        binding.index = data["hat"].get<int>() * 4 +
                        static_cast<int>(found - std::begin(kHatDirections));
        return binding;
    }

    if (!input.is_number_integer() || input.get<int>() < 0) {
        return std::nullopt;
    }

    binding.index = input.get<int>();
    return binding;
}

json bindingToJson(const Binding& binding) {
    const auto source = std::find_if(
        std::begin(kSourceNames), std::end(kSourceNames),
        [&](const SourceName& entry) { return binding.source == entry.source; });

    json data = {{"source", source->name}, {"scale", binding.scale}};

    // Both return null for a number SDL does not know, which reads back as
    // an unreadable binding and is skipped.
    const char* name = nullptr;

    switch (binding.source) {
        case Source::Button:
            name = SDL_GetGamepadStringForButton(
                static_cast<SDL_GamepadButton>(binding.index));
            data["input"] = name ? name : "";
            break;

        case Source::Axis:
            name = SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(binding.index));
            data["input"] = name ? name : "";
            break;

        case Source::JoyHat:
            data["hat"] = binding.index / 4;
            data["direction"] = kHatDirections[binding.index % 4];
            break;

        case Source::JoyButton:
        case Source::JoyAxis:
        case Source::Key:
            data["input"] = binding.index;
            break;
    }

    if (binding.fromMin) {
        data["from_min"] = true;
    }

    if (!binding.device.empty()) {
        data["device"] = binding.device;
    }

    return data;
}

}  // namespace

void refreshProfiles(Profiles& profiles) {
    profiles.names.clear();

    std::error_code ec;

    for (const fs::directory_entry& entry : fs::directory_iterator(profiles.dir, ec)) {
        if (entry.path().extension() == ".json") {
            profiles.names.push_back(entry.path().stem().string());
        }
    }

    std::sort(profiles.names.begin(), profiles.names.end());
}

bool loadProfile(Profiles& profiles, const std::string& name) {
    if (name.empty() || !validCharacters(name)) {
        return false;
    }

    std::ifstream input(profilePath(profiles, name));

    if (!input) {
        return false;
    }

    // No exceptions, a parse error comes back as a discarded value.
    const json data = json::parse(input, nullptr, false);

    if (!data.is_object()) {
        std::cerr << "Profile is not valid JSON: " << profilePath(profiles, name) << std::endl;
        return false;
    }

    Profile profile;
    profile.name = name;

    if (data.contains("deadzone") && data["deadzone"].is_number()) {
        profile.deadzone = std::clamp(data["deadzone"].get<float>(), 0.0f, 0.9f);
    }

    if (data.contains("bindings") && data["bindings"].is_object()) {
        for (size_t i = 0; i < kActionCount; ++i) {
            const char* code = kActions[i].code;

            if (!data["bindings"].contains(code) || !data["bindings"][code].is_array()) {
                continue;
            }

            for (const json& entry : data["bindings"][code]) {
                if (const std::optional<Binding> binding = readBinding(entry)) {
                    profile.bindings[i].push_back(*binding);
                }
            }
        }
    }

    profiles.current = profile;
    profiles.saved = profile;
    return true;
}

void useNoProfile(Profiles& profiles) {
    profiles.current = Profile{};
    profiles.saved = profiles.current;
}

bool saveProfile(Profiles& profiles) {
    const Profile& profile = profiles.current;

    if (profile.name.empty()) {
        return false;
    }

    std::error_code ec;
    fs::create_directories(profiles.dir, ec);

    std::ofstream output(profilePath(profiles, profile.name));

    if (!output) {
        std::cerr << "Failed to open profile for writing: "
                  << profilePath(profiles, profile.name) << std::endl;
        return false;
    }

    json bindings = json::object();

    for (size_t i = 0; i < kActionCount; ++i) {
        json list = json::array();

        // Keys belong to keyboard mode's fixed layout, never to a profile.
        for (const Binding& binding : profile.bindings[i]) {
            if (binding.source != Source::Key) {
                list.push_back(bindingToJson(binding));
            }
        }

        bindings[kActions[i].code] = list;
    }

    output << json{{"deadzone", profile.deadzone}, {"bindings", bindings}}.dump(4)
           << std::endl;

    if (!output.good()) {
        return false;
    }

    profiles.saved = profile;
    refreshProfiles(profiles);
    return true;
}

bool renameProfile(Profiles& profiles, const std::string& name) {
    std::error_code ec;
    fs::rename(
        profilePath(profiles, profiles.current.name), profilePath(profiles, name), ec);

    if (ec) {
        std::cerr << "Failed to rename profile: " << ec.message() << std::endl;
        return false;
    }

    profiles.current.name = name;
    profiles.saved.name = name;
    refreshProfiles(profiles);
    return true;
}

bool deleteProfile(Profiles& profiles) {
    std::error_code ec;

    if (!fs::remove(profilePath(profiles, profiles.current.name), ec)) {
        return false;
    }

    useNoProfile(profiles);
    refreshProfiles(profiles);
    return true;
}

std::string profileNameProblem(
    const Profiles& profiles, const std::string& name, const std::string& except) {
    if (name.empty()) {
        return "Enter a name.";
    }

    if (name.size() > kMaxProfileName) {
        return "Use 40 characters or fewer.";
    }

    if (!validCharacters(name)) {
        return "Use only letters, numbers, spaces, - and _.";
    }

    if (name.front() == ' ' || name.back() == ' ') {
        return "Remove the space at the start or end.";
    }

    if (equalsNoCase(name, "None")) {
        return "None is the built-in profile with no bindings.";
    }

    // Ignoring case, because macOS and Windows treat Peyton.json and
    // peyton.json as the same file.
    for (const std::string& existing : profiles.names) {
        if (equalsNoCase(existing, name) && !equalsNoCase(existing, except)) {
            return "A profile named " + existing + " already exists.";
        }
    }

    return "";
}
