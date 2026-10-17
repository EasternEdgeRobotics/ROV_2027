#pragma once

#include "Controls.hpp"

#include <filesystem>
#include <string>
#include <vector>

constexpr size_t kMaxProfileName = 40;

// Each profile is one file, <dir>/<name>.json, so a pilot can carry theirs to
// another laptop by copying one file. The name is the filename and is not
// stored inside it.
struct Profiles {
    std::filesystem::path dir;

    // What the controls read and the bindings editor edits. An empty name is
    // the built-in None profile, which has no file.
    Profile current;

    // `current` as it was last loaded or saved.
    Profile saved;

    // Profiles on disk, sorted.
    std::vector<std::string> names;

    bool unsaved() const { return !(current == saved); }
};

// Reads the profile names from disk.
void refreshProfiles(Profiles& profiles);

// Makes `name` current. A missing or malformed key in the file keeps its
// default, and a binding that cannot be read is skipped. Returns false if
// the file cannot be read, leaving `current` alone.
bool loadProfile(Profiles& profiles, const std::string& name);

// The built-in profile with no bindings, which every launch starts on.
void useNoProfile(Profiles& profiles);

// Writes `current`, which must have a name.
bool saveProfile(Profiles& profiles);

// Renames the current profile and its file.
bool renameProfile(Profiles& profiles, const std::string& name);

// Deletes the current profile's file and switches to None.
bool deleteProfile(Profiles& profiles);

// Empty when `name` can be used for a new profile, otherwise why not.
// `except` is a name the check ignores, for renaming a profile to itself.
std::string profileNameProblem(
    const Profiles& profiles, const std::string& name, const std::string& except = "");
