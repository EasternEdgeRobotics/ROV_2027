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

## Files
| | |
|---|---|
| `src/main.cpp` | The window layout, the settings window and the key handling |
| `src/Config.cpp` | Loading and saving the config file |

The config file is `frontend_config.json` in `~/Library/Application Support/Eastern Edge` on macOS, `%APPDATA%\Eastern Edge` on Windows, and `~/.config/eastern-edge` on Linux. Set `EER_CONFIG_DIR` to use another directory.
