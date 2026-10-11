#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;

layout(set = 2, binding = 0) uniform sampler2D tex_y;
layout(set = 2, binding = 1) uniform sampler2D tex_uv;

void main() {
    float y = texture(tex_y, v_uv).r;
    vec2 uv = texture(tex_uv, v_uv).rg;
    float cb = uv.x - 0.5;
    float cr = uv.y - 0.5;
    float r = y + 1.402 * cr;
    float g = y - 0.34414 * cb - 0.71414 * cr;
    float b = y + 1.772 * cb;
    frag_color = vec4(clamp(vec3(r, g, b), 0.0, 1.0), 1.0);
}