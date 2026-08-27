#version 460

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

// Descriptor-free validation shader. The production terrain path supplies
// streamed height/material pages; this procedural analogue exercises the same
// biome blending, derivative-normal, and atmospheric response.
float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1.0, 0.0)), u.x),
               mix(hash12(i + vec2(0.0, 1.0)), hash12(i + 1.0), u.x), u.y);
}

float fbm(vec2 p) {
    float value = 0.0;
    float amplitude = 0.52;
    mat2 rotation = mat2(0.80, -0.60, 0.60, 0.80);
    for (int octave = 0; octave < 5; ++octave) {
        value += amplitude * valueNoise(p);
        p = rotation * p * 2.03 + 17.1;
        amplitude *= 0.49;
    }
    return value;
}

void main() {
    vec2 mapUv = uv;
    vec2 world = (mapUv - 0.5) * vec2(7.2, 4.2);
    float continental = fbm(world * 0.72) * 0.72 + fbm(world * 2.15) * 0.28;
    float ridges = 1.0 - abs(fbm(world * 3.8) * 2.0 - 1.0);
    float height = continental * 0.78 + ridges * ridges * 0.22;
    float moisture = fbm(world * 1.15 + vec2(41.0, -19.0));

    float dx = dFdx(height);
    float dy = dFdy(height);
    vec3 normal = normalize(vec3(-dx * 78.0, -dy * 78.0, 1.0));
    float slope = 1.0 - normal.z;

    vec3 dryGrass = vec3(0.31, 0.34, 0.16);
    vec3 wetGrass = vec3(0.105, 0.245, 0.105);
    vec3 forest = vec3(0.045, 0.145, 0.075);
    vec3 soil = vec3(0.31, 0.245, 0.155);
    vec3 rock = vec3(0.34, 0.35, 0.34);
    vec3 snow = vec3(0.78, 0.82, 0.82);

    vec3 lowland = mix(dryGrass, wetGrass, smoothstep(0.34, 0.72, moisture));
    lowland = mix(lowland, forest, smoothstep(0.58, 0.82, moisture) * smoothstep(0.30, 0.52, height));
    vec3 albedo = mix(lowland, soil, smoothstep(0.13, 0.34, slope));
    albedo = mix(albedo, rock, clamp(smoothstep(0.58, 0.80, height) + smoothstep(0.28, 0.55, slope), 0.0, 1.0));
    albedo = mix(albedo, snow, smoothstep(0.82, 0.96, height));

    vec3 sunDirection = normalize(vec3(-0.42, -0.31, 0.85));
    float diffuse = max(dot(normal, sunDirection), 0.0);
    float sky = 0.48 + 0.52 * normal.z;
    float contact = mix(0.78, 1.0, smoothstep(0.25, 0.80, height));
    vec3 lit = albedo * (0.22 + 0.72 * diffuse + 0.20 * sky) * contact;

    float distanceFromFocus = smoothstep(0.18, 0.92, length(mapUv - vec2(0.50, 0.46)));
    vec3 haze = vec3(0.31, 0.42, 0.48);
    lit = mix(lit, haze, distanceFromFocus * 0.16);
    outColor = vec4(lit, 1.0);
}
