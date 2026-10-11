// HLSL mirror of nv12.vert, compiled to DXIL for SDL_GPU's D3D12 backend.
// Keep the math in sync with shaders/nv12.vert and kVertMsl in GpuShared.cpp.

struct VertOut {
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

VertOut main(uint vid : SV_VertexID) {
    // Fullscreen triangle from the vertex index (0, 1, 2).
    float2 uv = float2((vid << 1) & 2, vid & 2);

    VertOut output;
    output.uv = uv;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}
