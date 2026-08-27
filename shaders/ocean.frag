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

    vec3 viewDirection = normalize(vec3((uv - 0.5) * vec2(0.75, 0.42), 1.15));
    float fresnel = pow(1.0 - max(dot(normal, viewDirection), 0.0), 4.0);
    vec3 sunDirection = normalize(vec3(-0.5, -0.25, 0.83));
    vec3 halfVector = normalize(sunDirection + viewDirection);
    float specular = pow(max(dot(normal, halfVector), 0.0), 64.0);

    // The validation renderer has no coast-distance texture, so a stable
    // analytic shelf stands in for the production shallow-water field.
    float shelf = smoothstep(0.08, 0.62,
        0.5 + 0.34 * sin(uv.x * 6.2) + 0.20 * sin(uv.y * 8.1 + uv.x * 2.0));
    vec3 deepWater = vec3(0.025, 0.135, 0.235);
    vec3 shallowWater = vec3(0.075, 0.34, 0.39);
    vec3 skyReflection = vec3(0.30, 0.47, 0.59);
    vec3 color = mix(deepWater, shallowWater, shelf * 0.58);
    color = mix(color, skyReflection, fresnel * 0.58);
    color += specular * vec3(0.85, 0.91, 0.86) * 0.52;

    float crest = smoothstep(0.70, 0.92, broad) * smoothstep(0.35, 0.82, shelf);
    color = mix(color, vec3(0.72, 0.82, 0.80), crest * 0.28);
    float alpha = 0.12 + shelf * 0.14 + fresnel * 0.10;
    outColor = vec4(color, alpha);
}
