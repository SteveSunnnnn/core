#version 460

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

float wave(vec2 p, vec2 direction, float frequency) {
    return sin(dot(p, normalize(direction)) * frequency);
}

void main() {
    vec2 p = uv * vec2(1.0, 0.72);
    float broad = wave(p, vec2(1.0, 0.34), 48.0) * 0.52
                + wave(p, vec2(-0.42, 1.0), 73.0) * 0.29
                + wave(p, vec2(0.77, -0.63), 121.0) * 0.19;
    float fine = wave(p, vec2(1.0, -0.12), 247.0);
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
    color += normal.xy.xyx * vec3(0.004, 0.006, 0.004);
    float alpha = 0.055 + shelf * 0.055;
    outColor = vec4(color, alpha);
}
