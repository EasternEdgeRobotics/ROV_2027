# Surface frontend
The main control interface for Eastern Edge's ROVs.

## Building
See [docs/Building.md](docs/Building.md) for dependencies, build commands and configure options.

## Running
```sh
./build/apps/frontend/eer_frontend.app/Contents/MacOS/eer_frontend   # macOS
./build/apps/frontend/eer_frontend                                   # Linux
build\apps\frontend\Release\eer_frontend.exe                         # Windows
```
Open Windows > Settings (F1) and enter a WHEP URL for each camera. See [docs/Cameras.md](docs/Cameras.md).

## Captures
The Capture buttons save the camera's current frame as a TIFF named `captures_<time>_section_<n>_Camera_<n>.tiff`, in `Pictures/eer_captures/<start time>/section_<n>/`. A section is one pass over a subject, so photogrammetry can be run on one folder. Next Section starts a new folder and resets the photo count.

## Profiles
A profile is one pilot's controller bindings, edited in Settings > Controls. Each is a file, `profiles/<name>.json` in the config directory, so copy that file to move a profile to another laptop.

Every launch starts on None, which has no bindings. Pass `--profile <name>` to start on a profile instead. For a launcher per pilot, make a copy of the frontend's `.desktop` file for each person with `Exec=eer_frontend --profile <name>`.

## Files
| | |
|---|---|
| `src/main.cpp` | The window layout, the settings window and the key handling |
| `src/Config.cpp` | Loading and saving the config file |
| `src/Controls.cpp` | The actions, reading controllers and the keyboard mode layout |
| `src/Profiles.cpp` | Loading and saving profiles |
| `src/BindingsEditor.cpp` | The profile picker and bindings editor in Settings > Controls |
| `src/Pilot.cpp` | Power, fast mode and the co-pilot window |
| `src/DummyRov.cpp` | A pretend ROV that flies on the pilot's output |

The config file is `frontend_config.json` in `~/Library/Application Support/Eastern Edge` on macOS, `%APPDATA%\Eastern Edge` on Windows, and `~/.config/eastern-edge` on Linux. Set `EER_CONFIG_DIR` to use another directory.
