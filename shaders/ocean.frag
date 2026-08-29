#version 460

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

// pc.view is consumed by fullscreen.vert; pc.params reserves a fragment-stage
// range so the layout matches the other scene passes.
layout(push_constant) uniform Push {
    vec4 view;
    vec4 params;
} pc;

// Wave directions are constants, so the unit vectors are folded in here rather
// than recomputed with a per-pixel normalize() four times every frame.
const vec2 kDirBroad = vec2(0.946, 0.322);   // normalize(vec2( 1.00, 0.34))
const vec2 kDirSwell = vec2(-0.387, 0.922);  // normalize(vec2(-0.42, 1.00))
const vec2 kDirChop = vec2(0.774, -0.634);   // normalize(vec2( 0.77,-0.63))
const vec2 kDirFine = vec2(0.993, -0.119);   // normalize(vec2( 1.00,-0.12))

float wave(vec2 p, vec2 unit_direction, float frequency) {
    return sin(dot(p, unit_direction) * frequency);
}

void main() {
    vec2 p = uv * vec2(1.0, 0.72);
    float broad = wave(p, kDirBroad, 48.0) * 0.52
                + wave(p, kDirSwell, 73.0) * 0.29
                + wave(p, kDirChop, 121.0) * 0.19;
    float fine = wave(p, kDirFine, 247.0);
    float height = broad * 0.72 + fine * 0.08;
    vec3 normal = normalize(vec3(-dFdx(height) * 8.0, -dFdy(height) * 8.0, 1.0));

    // A translucent, oxidized blue-green ink wash printed over the paper.
    // No cold specular highlight: this pass belongs to an atlas, not a steel
    // or photoreal water surface.
    float shelf = smoothstep(0.08, 0.62,
        0.5 + 0.34 * sin(uv.x * 6.2) + 0.20 * sin(uv.y * 8.1 + uv.x * 2.0));
    vec3 deepInk = vec3(0.105, 0.170, 0.155);
    vec3 shallowInk = vec3(0.235, 0.310, 0.245);
    vec3 color = mix(deepInk, shallowInk, shelf * 0.66);

    // Surface sheen from the wave normal. The original wrote `normal.xy.xyx`,
    // which duplicated the x component and dropped z entirely; the intent is a
    // two-axis sheen, so only xy contributes.
    color += vec3(normal.x, normal.y, 0.0) * vec3(0.004, 0.006, 0.004);

    float alpha = 0.055 + shelf * 0.055;
    outColor = vec4(color, alpha);
}
