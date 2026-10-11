#pragma once

#include <SDL3/SDL.h>

struct GpuShared {
    SDL_GPUDevice* device = nullptr;
    SDL_GPUGraphicsPipeline* nv12Pipeline = nullptr;
    SDL_GPUSampler* sampler = nullptr;
};

// Pass this to AppInfo::shaderFormats. DXIL is only offered when the DXIL
// shaders were built, otherwise SDL could pick D3D12 and then fail to create
// the NV12 pipeline.
SDL_GPUShaderFormat videoShaderFormats();

// Call once before cameras start, and shut down after they stop.
bool initGpuShared(SDL_GPUDevice* device);
void shutdownGpuShared();
GpuShared& gpuShared();