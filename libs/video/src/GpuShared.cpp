#include <eer/video/GpuShared.hpp>

#include "nv12_vert_spv.h"
#include "nv12_frag_spv.h"

#if defined(EER_VIDEO_HAVE_DXIL)
#include "nv12_vert_dxil.h"
#include "nv12_frag_dxil.h"
#define NV12_VERT_DXIL     nv12_vert_dxil
#define NV12_VERT_DXIL_LEN nv12_vert_dxil_len
#define NV12_FRAG_DXIL     nv12_frag_dxil
#define NV12_FRAG_DXIL_LEN nv12_frag_dxil_len
#else
#define NV12_VERT_DXIL     nullptr
#define NV12_VERT_DXIL_LEN 0
#define NV12_FRAG_DXIL     nullptr
#define NV12_FRAG_DXIL_LEN 0
#endif

#include <cstring>
#include <iostream>

namespace {

GpuShared gShared;

const char* kVertMsl = R"(
#include <metal_stdlib>
using namespace metal;

struct VertOut {
    float4 position [[position]];
    float2 uv;
};

vertex VertOut vertex_main(uint vid [[vertex_id]]) {
    VertOut out;
    float2 uv = float2((vid << 1) & 2, vid & 2);
    out.uv = uv;
    out.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return out;
}
)";

const char* kFragMsl = R"(
#include <metal_stdlib>
using namespace metal;

struct VertOut {
    float4 position [[position]];
    float2 uv;
};

fragment float4 fragment_main(
    VertOut in [[stage_in]],
    texture2d<float> tex_y [[texture(0)]],
    texture2d<float> tex_uv [[texture(1)]],
    sampler samp_y [[sampler(0)]],
    sampler samp_uv [[sampler(1)]])
{
    float y = tex_y.sample(samp_y, in.uv).r;
    float2 uv = tex_uv.sample(samp_uv, in.uv).rg;
    float cb = uv.x - 0.5;
    float cr = uv.y - 0.5;
    float r = y + 1.402 * cr;
    float g = y - 0.34414 * cb - 0.71414 * cr;
    float b = y + 1.772 * cb;
    return float4(clamp(float3(r, g, b), 0.0, 1.0), 1.0);
}
)";

SDL_GPUShader* makeShader(
    SDL_GPUDevice* device,
    SDL_GPUShaderStage stage,
    const uint8_t* spirv,
    size_t spirvLen,
    const unsigned char* dxil,
    size_t dxilLen,
    const char* msl,
    const char* mslEntry,
    uint32_t numSamplers)
{
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);

    SDL_GPUShaderCreateInfo info = {};
    info.stage = stage;
    info.num_samplers = numSamplers;

    if (formats & SDL_GPU_SHADERFORMAT_SPIRV) {
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.code = spirv;
        info.code_size = spirvLen;
        info.entrypoint = "main";
    } else if ((formats & SDL_GPU_SHADERFORMAT_DXIL) && dxil) {
        info.format = SDL_GPU_SHADERFORMAT_DXIL;
        info.code = dxil;
        info.code_size = dxilLen;
        info.entrypoint = "main";
    } else if (formats & SDL_GPU_SHADERFORMAT_MSL) {
        info.format = SDL_GPU_SHADERFORMAT_MSL;
        info.code = reinterpret_cast<const Uint8*>(msl);
        info.code_size = std::strlen(msl);
        info.entrypoint = mslEntry;
    } else {
        std::cerr << "No supported GPU shader format (need SPIRV, DXIL, or MSL)."
                  << " Device reports formats: " << formats
                  << ". If this is Windows, rebuild with -DEER_VIDEO_ENABLE_DXIL=ON."
                  << std::endl;
        return nullptr;
    }

    SDL_GPUShader* shader = SDL_CreateGPUShader(device, &info);

    if (!shader) {
        std::cerr << "SDL_CreateGPUShader failed: " << SDL_GetError()
                  << std::endl;
    }

    return shader;
}

} // namespace

GpuShared& gpuShared() {
    return gShared;
}

SDL_GPUShaderFormat videoShaderFormats() {
    SDL_GPUShaderFormat formats =
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL;

#if defined(EER_VIDEO_HAVE_DXIL)
    formats |= SDL_GPU_SHADERFORMAT_DXIL;
#endif

    return formats;
}

bool initGpuShared(SDL_GPUDevice* device) {
    gShared.device = device;

    SDL_GPUShader* vert = makeShader(
        device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        nv12_vert_spv,
        nv12_vert_spv_len,
        NV12_VERT_DXIL,
        NV12_VERT_DXIL_LEN,
        kVertMsl,
        "vertex_main",
        0);

    SDL_GPUShader* frag = makeShader(
        device,
        SDL_GPU_SHADERSTAGE_FRAGMENT,
        nv12_frag_spv,
        nv12_frag_spv_len,
        NV12_FRAG_DXIL,
        NV12_FRAG_DXIL_LEN,
        kFragMsl,
        "fragment_main",
        2);

    if (!vert || !frag) {
        return false;
    }

    SDL_GPUColorTargetDescription colorTarget = {};
    colorTarget.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.vertex_shader = vert;
    pipelineInfo.fragment_shader = frag;
    pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipelineInfo.target_info.num_color_targets = 1;
    pipelineInfo.target_info.color_target_descriptions = &colorTarget;

    gShared.nv12Pipeline =
        SDL_CreateGPUGraphicsPipeline(device, &pipelineInfo);

    SDL_ReleaseGPUShader(device, vert);
    SDL_ReleaseGPUShader(device, frag);

    if (!gShared.nv12Pipeline) {
        std::cerr << "Failed to create NV12 pipeline: " << SDL_GetError()
                  << std::endl;
        return false;
    }

    SDL_GPUSamplerCreateInfo samplerInfo = {};
    samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

    gShared.sampler = SDL_CreateGPUSampler(device, &samplerInfo);

    return gShared.sampler != nullptr;
}

void shutdownGpuShared() {
    if (!gShared.device) return;

    if (gShared.sampler) {
        SDL_ReleaseGPUSampler(gShared.device, gShared.sampler);
        gShared.sampler = nullptr;
    }

    if (gShared.nv12Pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(gShared.device, gShared.nv12Pipeline);
        gShared.nv12Pipeline = nullptr;
    }

    gShared.device = nullptr;
}