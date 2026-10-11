#pragma once

#include <SDL3/SDL.h>
#include "imgui.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace image {

// One GPU texture, released on destruction or replacement. Not copyable,
// because two of these freeing the same texture would be a double free.
class Texture {
public:
    Texture() = default;
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    // Decodes anything stb_image reads (PNG, JPEG, ...) and replaces what was
    // held. Returns false and keeps the previous texture on failure.
    bool load(SDL_GPUDevice* device, const void* data, size_t length);

    // Must be called before the GPU device is destroyed, since releasing a
    // texture needs the device that made it.
    void reset();

    bool valid() const { return texture_ != nullptr; }
    int width() const { return width_; }
    int height() const { return height_; }

    // The ImGui SDLGPU3 backend takes the SDL_GPUTexture* itself as the ID.
    ImTextureID id() const { return (ImTextureID)(intptr_t)texture_; }

private:
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUTexture* texture_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

// A decoded image on the CPU, RGBA, eight bits a channel.
struct Bitmap {
    std::vector<unsigned char> pixels;  // width * height * 4
    int width = 0;
    int height = 0;
};

bool decode(const void* data, size_t length, Bitmap& out);

}  // namespace image
