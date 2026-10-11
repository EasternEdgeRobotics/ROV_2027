#include <eer/app/Image.hpp>

// Exactly one translation unit may define this, so it lives here and not in
// the header.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>


#include <cstdio>
#include <cstring>
#include <iostream>

namespace image {

Texture::~Texture() {
    reset();
}

void Texture::reset() {
    if (!texture_) {
        return;
    }

    // SDL defers the free until the GPU is done with it, so this is safe for
    // a texture the frame in flight is still sampling.
    SDL_ReleaseGPUTexture(device_, texture_);

    texture_ = nullptr;
    width_ = 0;
    height_ = 0;
}

bool Texture::load(SDL_GPUDevice* device, const void* data, size_t length) {
    int width = 0;
    int height = 0;
    int channels = 0;

    // Forced to 4 channels to match the RGBA texture below.
    unsigned char* pixels = stbi_load_from_memory(
        static_cast<const unsigned char*>(data),
        static_cast<int>(length),
        &width,
        &height,
        &channels,
        4);

    if (!pixels) {
        std::cerr << "Image: could not decode: " << stbi_failure_reason()
                  << std::endl;
        return false;
    }

    SDL_GPUTextureCreateInfo texInfo = {};
    texInfo.type = SDL_GPU_TEXTURETYPE_2D;
    texInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    texInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texInfo.width = static_cast<Uint32>(width);
    texInfo.height = static_cast<Uint32>(height);
    texInfo.layer_count_or_depth = 1;
    texInfo.num_levels = 1;

    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &texInfo);

    if (!texture) {
        std::cerr << "Image: could not create texture: " << SDL_GetError()
                  << std::endl;
        stbi_image_free(pixels);
        return false;
    }

    const size_t byteCount = static_cast<size_t>(width) * height * 4;

    SDL_GPUTransferBufferCreateInfo tbInfo = {};
    tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbInfo.size = static_cast<Uint32>(byteCount);

    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &tbInfo);

    if (!transfer) {
        std::cerr << "Image: could not create transfer buffer: "
                  << SDL_GetError() << std::endl;
        SDL_ReleaseGPUTexture(device, texture);
        stbi_image_free(pixels);
        return false;
    }

    void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);

    if (!mapped) {
        std::cerr << "Image: could not map transfer buffer: "
                  << SDL_GetError() << std::endl;
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        SDL_ReleaseGPUTexture(device, texture);
        stbi_image_free(pixels);
        return false;
    }

    std::memcpy(mapped, pixels, byteCount);
    SDL_UnmapGPUTransferBuffer(device, transfer);
    stbi_image_free(pixels);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTextureTransferInfo src = {};
    src.transfer_buffer = transfer;

    SDL_GPUTextureRegion dst = {};
    dst.texture = texture;
    dst.w = static_cast<Uint32>(width);
    dst.h = static_cast<Uint32>(height);
    dst.d = 1;

    SDL_UploadToGPUTexture(copyPass, &src, &dst, false);
    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device, transfer);

    // Only now that the new one is good, so a failed load keeps the old one.
    reset();

    device_ = device;
    texture_ = texture;
    width_ = width;
    height_ = height;

    return true;
}

bool decode(const void* data, size_t length, Bitmap& out) {
    int width = 0;
    int height = 0;
    int channels = 0;

    unsigned char* pixels = stbi_load_from_memory(
        static_cast<const unsigned char*>(data),
        static_cast<int>(length),
        &width, &height, &channels, 4);

    if (!pixels) {
        return false;
    }

    out.width = width;
    out.height = height;
    out.pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);

    stbi_image_free(pixels);
    return true;
}

}  // namespace image
