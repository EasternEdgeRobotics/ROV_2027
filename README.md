# ROV_2027
[Eastern Edge Robotics](https://www.easternedgerobotics.com/) is a student team from Memorial University of Newfoundland and the Marine Institute that competes in the MATE ROV Competition. This is the team's 2027 software.

## Getting started
SDL, Dear ImGui, nlohmann/json and stb are submodules, so clone with them:
```sh
git clone --recurse-submodules https://github.com/EasternEdgeRobotics/ROV_2027.git
```
In a clone that already exists, run `git submodule update --init --recursive`.

To build the frontend, install the dependencies for your platform from [apps/frontend/docs/Building.md](apps/frontend/docs/Building.md), then:
```sh
cmake -S . -B build
cmake --build build -j
```

## Contents
| | |
|---|---|
| [apps/frontend](apps/frontend/README.md) | The surface frontend. [Building](apps/frontend/docs/Building.md), [cameras](apps/frontend/docs/Cameras.md) |
| `libs/app`, `libs/video` | What the frontend is built from, and what a second app would reuse |
| [docs/Platform_Support.md](docs/Platform_Support.md) | Which platforms are supported, and how well |
| [docs/BlueStar.md](docs/BlueStar.md) | The 2026 ROV, used for development until the 2027 one exists |
