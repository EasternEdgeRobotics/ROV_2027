# OS Support for Eastern Edge Software
For a platform to be supported, it must have:
- SDL3 Support
- SDL3 GPU Support
- Vulkan, Direct3D12 or Metal 2 Mac2 support (Many systems made after 2012, most after 2015)
- Python 3.11+
- CMake 3.25+

## Tier 1
Platforms that should always work, and must be tested before merging to main.
The full set of features must be available, with the exception of features locked behind specific OSes, ie Apple RealityKit in 2026.
- Linux (Ubuntu 26.04, x86_64)
- macOS 27+ (arm64)

## Tier 2
Platforms that should always work, but might break at times.
The full set of features should be available, with the exception of features locked behind specific OSes.
- Windows 10+ (x86_64)
- Linux (arm64)

## Tier 3
Platforms that ideally work, but aren't guaranteed to work.
The full set of features is not required.
- Windows 10+ (arm64)
- macOS 14-26
- iOS/iPadOS 15+ (A9+)
- visionOS 1+


## Linux
Linux referes to any distro/setup w/ Gstreamer (Either webrtcbin from gst-plugins-bad w/ libnice or whepclientsrc from gst-plugins-rs), glslc (from shaderc), functional Vulkan, uv support, and a C++20 compiler.