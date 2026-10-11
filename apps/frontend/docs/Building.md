# Building the frontend
Run every command from the repository root, after cloning with submodules.

## Dependencies
### macOS
```sh
brew install cmake gstreamer libnice libnice-gstreamer libtiff shaderc
```

### Ubuntu
```sh
sudo apt install build-essential cmake git pkg-config glslc \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev \
    gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-nice \
    libtiff-dev libcurl4-openssl-dev mesa-vulkan-drivers \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev \
    libxkbcommon-dev libwayland-dev libdecor-0-dev libdrm-dev libgbm-dev
```

### Fedora
```sh
sudo dnf install @development-tools cmake git pkgconf glslc \
    gstreamer1-devel gstreamer1-plugins-base-devel gstreamer1-plugins-bad-free-devel \
    gstreamer1-plugins-base gstreamer1-plugins-good gstreamer1-plugins-bad-free libnice-gstreamer1 libnice \
    libtiff-devel libcurl-devel mesa-vulkan-drivers \
    libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXi-devel libXfixes-devel \
    libxkbcommon-devel wayland-devel libdecor-devel libdrm-devel mesa-libgbm-devel
```
H.264 decoding needs RPM Fusion: `sudo dnf install gstreamer1-plugins-bad-freeworld gstreamer1-plugin-openh264`.

### Windows
From an administrator PowerShell:
```
winget install Git.Git KhronosGroup.VulkanSDK
winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --quiet --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```
The Vulkan SDK provides `glslc` and `dxc`. GStreamer is not on winget, so download the MSVC x86_64 installer from https://gstreamer.freedesktop.org/download/ and select the development files as well as the runtime.

The rest come from vcpkg:
```
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg install tiff:x64-windows curl:x64-windows pkgconf:x64-windows
```

## macOS and Linux
```sh
cmake -S . -B build
cmake --build build -j
```
On macOS the app is a bundle, `build/apps/frontend/eer_frontend.app`. Run `Contents/MacOS/eer_frontend` inside it to keep the log in your terminal.

The layered macOS icon needs `actool`, which ships only with full Xcode. Without it the build uses the flat PNG and says so at configure time.

## Windows
In a PowerShell opened after the installers have finished:
```
$env:PATH += ";C:\vcpkg\installed\x64-windows\tools\pkgconf"
$env:PKG_CONFIG_PATH = "$env:GSTREAMER_1_0_ROOT_MSVC_X86_64\lib\pkgconfig;C:\vcpkg\installed\x64-windows\lib\pkgconfig"

cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Release
```
The binary is `build\apps\frontend\Release\eer_frontend.exe`. GStreamer's `bin` directory must be on `PATH` to run it.

## Installing on Linux
```sh
cmake --install build --prefix ~/.local
```
This installs the binary, a launcher entry and the icons, so the app shows up in the applications menu with its icon. The icons are only installed if `assets/trident/trident-<size>.png` exist for 48, 128, 256 and 512. [assets/README.md](../../../assets/README.md) has the command that makes them.

## Configure options
| Option | Default | What it does |
|---|---|---|
| `CMAKE_BUILD_TYPE` | `RelWithDebInfo` | Set for you when empty, because an unoptimised build decodes video far too slowly to use |
| `EER_VIDEO_BACKEND` | `webrtcbin` | `webrtcbin` or `whepclientsrc` |
| `EER_VIDEO_ENABLE_DXIL` | `ON` on Windows | Compile the video shaders for Direct3D 12. Needs `dxc`. When off, Windows needs a working Vulkan driver |

`whepclientsrc` comes from gst-plugins-rs, which Homebrew includes and Ubuntu does not package. A missing plugin is not caught at build time: the build succeeds and the cameras never connect. `gst-inspect-1.0 whepclientsrc` tells you whether it is installed.
