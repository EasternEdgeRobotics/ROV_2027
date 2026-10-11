// HLSL mirror of nv12.frag, compiled to DXIL for SDL_GPU's D3D12 backend.
// Keep the conversion constants in sync with shaders/nv12.frag and kFragMsl
// in GpuShared.cpp.
//
// SDL_GPU binds fragment-stage sampled textures in register space2, with the
// texture and its sampler sharing an index. This matches `set = 2` in the
// GLSL source.

Texture2D<float4> tex_y  : register(t0, space2);
SamplerState      samp_y : register(s0, space2);

Texture2D<float4> tex_uv  : register(t1, space2);
SamplerState      samp_uv : register(s1, space2);

struct VertOut {
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

float4 main(VertOut input) : SV_Target0 {
    float  y  = tex_y.Sample(samp_y, input.uv).r;
    float2 uv = tex_uv.Sample(samp_uv, input.uv).rg;

    float cb = uv.x - 0.5;
    float cr = uv.y - 0.5;

    float r = y + 1.402 * cr;
    float g = y - 0.34414 * cb - 0.71414 * cr;
    float b = y + 1.772 * cb;

    return float4(clamp(float3(r, g, b), 0.0, 1.0), 1.0);
}
