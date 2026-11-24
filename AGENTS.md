# Working in this repo

Eastern Edge Robotics's 2027 software. Right now that is the surface frontend, the StarStruck photo booth and the two libraries they are built from.

## Layout
| | |
|---|---|
| `apps/frontend` | The surface frontend: four cameras, captures, settings. Its own docs are in `apps/frontend/docs/` |
| `apps/starstruck` | A photo booth on one ROV camera, with the ROV's lights as the flash and an Ivy 2 printer |
| `libs/app` | `eer_app`. Window, GPU device, ImGui frame, theme, config paths, image loading, small UI helpers. Needs only SDL and ImGui |
| `libs/video` | `eer_video`. WHEP streams, the camera widget, screenshots with EXIF. Needs GStreamer, libtiff and libcurl |
| `cmake/` | `eer_package_app()` and the scripts it runs |
| `assets/` | Icons and the no signal image |
| `third_party/` | SDL, Dear ImGui, nlohmann/json and stb as submodules |
| `docs/` | What is not about one component: ROV hardware notes, platform support, MATE tasks |

## Building
```sh
git submodule update --init --recursive
cmake -S . -B build
cmake --build build -j
./build/apps/frontend/eer_frontend.app/Contents/MacOS/eer_frontend   # macOS
./build/apps/frontend/eer_frontend                                   # Linux
```
Dependencies per platform and every configure option are in [apps/frontend/docs/Building.md](apps/frontend/docs/Building.md). Each component keeps its own docs beside it, in a `docs/` folder of its own. The top-level `docs/` is for what belongs to no single component. There are no tests yet, so a change is checked by building and running the frontend.

## Adding a second app
A new app is a directory under `apps/` with its own `main.cpp`, a `CMakeLists.txt` that links `eer::app` and calls `eer_package_app()`, and one `add_subdirectory` line in the top-level `CMakeLists.txt`. `apps/frontend` is the example to copy.

Link `eer::video` only if the app shows cameras. That split is deliberate: an app without it builds with nothing but SDL, which is what would let a deck manager run on iOS.

Code two apps both need goes in a library. Code only one app needs stays in that app.

## Complexity
The most proper solution is not automatically the best one here. A thorough design that nobody on the team understands is how code becomes unmaintainable, and the team turns over most of its members every year.

That does not mean doing everything the simplest possible way. A crude solution that has to be worked around is its own maintenance cost. Aim between the two: as much structure as the problem needs, and no more than the people maintaining it can follow.

- When the proper version and the plain version are close, take the plain one. When the plain one would be fragile or would be repeated in several places, do it properly and keep it readable.
- Add an abstraction, an option or a platform branch when something needs it, not in case something might.
- Keep files about one thing. `main.cpp` is the window layout, the settings UI and the key handling.
- If a design needs a long explanation before the code makes sense, treat that as a sign it is too much.
- The link to the ROV, pilot controls and telemetry are not here yet and are being designed separately. Do not start on them unasked.

## Comments
Short, plain, and only where the code does not explain itself. Do not explain a for loop.

A comment earns its place by saying why: the failure that motivated a line, the obvious approach that does not work, the thing that breaks if this is reordered. State it in a sentence or two and stop.

- No em dashes, and no `--` standing in for one. Use a comma or a second sentence.
- No banner comments or restating the function name.
- `libs/video/src/backend_webrtcbin.cpp` and `backend_whepclientsrc.cpp` came over from 2026 with their long comments intact. Leave them unless you are already rewriting the code they describe.

## Documentation voice
Docs follow the same measure as comments.

- No em dashes, and no `--` standing in for one. Use a comma or a second sentence.
- Be short. State what the thing does, then stop.
- Describe what a thing is now. Leave out where it came from, what it replaced, what it does not do yet and what is planned. A frontend README opens with "The main control interface for Eastern Edge's ROVs", not with its history.
- State the behaviour, not the case for it. "It is not editable in the app" is the whole sentence. Why it was built that way, and what other software does with the result, belong in a code comment if anywhere.
- A caveat stays only when it changes what the reader does. The measurements behind it go in a code comment.
- Do not repeat what the app already shows. The keys are listed in Settings > Controls, so no doc carries a key table, and a doc does not say where a setting is when the settings window makes that plain.
- Address the reader directly for anything they do: "Clone with submodules", "Set the audio caps to `none`".
- Use a table for any fixed set a reader might look up: options, keys, config fields.
- The possessive is "Eastern Edge Robotics's" and "Eastern Edge's".
- No hard wrapping, one line per paragraph. No blank line between a heading and its text. No bold or italics in prose. Headings are short noun phrases.

## MATE tasks
[docs/MATE_Tasks.md](docs/MATE_Tasks.md) is this season's tasks and scoring, copied from what MATE publishes. Use it to check what the software has to support.

MATE's manual is not the last word. Corrections arrive in forum posts and supporting documents, so when the file and a newer ruling disagree, the ruling wins and the file should be updated to match.

You may edit it. Follow the formatting already there: the "Last Update" line naming the date and the source, the contents list, and a points table per task. Record what MATE said, not what this repo does.

## Human-maintained doc
[docs/Improvements.md](docs/Improvements.md) lists the changes since 2026, for the technical documentation. Read it for context, never edit it. A change earns a line only if it changes what the ROV or its pilot can do. When one does, suggest the line in your reply.

## Code
- C++20, `#pragma once`, 4 space indent. `PascalCase` types, `camelCase` functions, `kConstant` constants. CMake uses 2 spaces and `EER_` prefixed options.
- Apps include library headers as `<eer/app/...>` and `<eer/video/...>`.
- `third_party/` is never edited. Update by bumping the checkout.
- Warnings (`-Wall -Wextra -Wpedantic`) apply to our targets only, and the build is kept free of them.
- Platform `#if`s stay inside the libraries.
- Keyboard input is read from SDL, through `App::keyPressed`, never through ImGui's key functions. ImGui is only asked whether a text field has focus. Two libraries each handling some of the keys is what the 2025 and 2026 frontends ended up with, and pilot controls will need SDL anyway.
- Settings live in the config file and are edited in the Settings window. `Camera` reads them from plain public fields each frame, so there is one copy of each setting.

## Platform support
Tier 1 is Linux x86_64 and macOS arm64, and a change that breaks either should not be merged. The full table is in [docs/Platform_Support.md](docs/Platform_Support.md).

## Commits
A subject line in the imperative, then a sentence or two on why, only when the diff does not make it obvious. No lists of touched files.
