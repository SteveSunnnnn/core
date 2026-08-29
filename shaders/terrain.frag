#version 460

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

// pc.view is the map viewport consumed by fullscreen.vert.
layout(push_constant) uniform Push {
    vec4 view;
    vec4 params;
} pc;

// Octave counts are specialization constants, not push-constant values.
// The original shader hard-coded `octave < 5`, which let the driver unroll the
// loop and fold the amplitude chain into constants. Moving the bound into a
// runtime uniform cost ~7% GPU time on its own, so the quality tier is baked
// in at pipeline creation instead and every tier still gets unrolled code.
layout(constant_id = 0) const int BASE_OCTAVES = 5;
layout(constant_id = 1) const int DETAIL_OCTAVES = 5;

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

// Each octave costs four hash evaluations, which makes the octave count the
// single most expensive knob in the terrain pass. Generating one function per
// constant keeps the bound literal so the loop is always unrolled.
#define FBM_DEFINE(NAME, OCTAVES)                    \
float NAME(vec2 p) {                                 \
    float value = 0.0;                               \
    float amplitude = 0.52;                          \
    mat2 rotation = mat2(0.80, -0.60, 0.60, 0.80);   \
    for (int octave = 0; octave < (OCTAVES); ++octave) { \
        value += amplitude * valueNoise(p);          \
        p = rotation * p * 2.03 + 17.1;              \
        amplitude *= 0.49;                           \
    }                                                \
    return value;                                    \
}

FBM_DEFINE(fbm_base, BASE_OCTAVES)
FBM_DEFINE(fbm_detail, DETAIL_OCTAVES)

void main() {
    vec2 mapUv = uv;
    vec2 world = (mapUv - 0.5) * vec2(7.2, 4.2);
    float continental = fbm_base(world * 0.72) * 0.72
                      + fbm_detail(world * 2.15) * 0.28;
    float ridges = 1.0 - abs(fbm_base(world * 3.8) * 2.0 - 1.0);
    float height = continental * 0.78 + ridges * ridges * 0.22;
    float moisture = fbm_detail(world * 1.15 + vec2(41.0, -19.0));

    float dx = dFdx(height);
    float dy = dFdy(height);
    vec3 normal = normalize(vec3(-dx * 78.0, -dy * 78.0, 1.0));
    float slope = 1.0 - normal.z;

    // Printed cartography rather than photoreal terrain: every biome is a
    // restrained ink wash over a warm archival paper stock.
    float paperCloud = fbm_detail(world * 0.34 + vec2(9.0, 27.0));
    float paperMottle = valueNoise(mapUv * vec2(17.0, 11.0) + vec2(5.0, 13.0));
    float paperGrain = hash12(gl_FragCoord.xy * 0.47) - 0.5;
    float fiber = sin(gl_FragCoord.y * 0.093 + valueNoise(world * 5.0) * 3.0);
    float crossFiber = sin(gl_FragCoord.x * 0.041 + paperCloud * 5.0);
    vec3 paperShadow = vec3(0.350, 0.270, 0.150);
    vec3 paperLight = vec3(0.490, 0.395, 0.225);
    vec3 paper = mix(paperShadow, paperLight, 0.28 + paperCloud * 0.58);
    paper *= 0.955 + paperMottle * 0.085;
    paper += paperGrain * 0.012 + fiber * 0.004 + crossFiber * 0.002;

    vec3 dryGrass = vec3(0.355, 0.355, 0.190);
    vec3 wetGrass = vec3(0.245, 0.325, 0.180);
    vec3 forest = vec3(0.175, 0.255, 0.145);
    vec3 soil = vec3(0.390, 0.295, 0.175);
    vec3 rock = vec3(0.360, 0.335, 0.260);
    vec3 snow = vec3(0.500, 0.465, 0.345);

    vec3 lowland = mix(dryGrass, wetGrass, smoothstep(0.34, 0.72, moisture));
    lowland = mix(lowland, forest, smoothstep(0.58, 0.82, moisture) * smoothstep(0.30, 0.52, height));
    vec3 albedo = mix(lowland, soil, smoothstep(0.13, 0.34, slope));
    albedo = mix(albedo, rock, clamp(smoothstep(0.58, 0.80, height) + smoothstep(0.28, 0.55, slope), 0.0, 1.0));
    albedo = mix(albedo, snow, smoothstep(0.82, 0.96, height));

    vec3 sunDirection = normalize(vec3(-0.42, -0.31, 0.85));
    float diffuse = max(dot(normal, sunDirection), 0.0);
    float sky = 0.48 + 0.52 * normal.z;
    float contact = mix(0.78, 1.0, smoothstep(0.25, 0.80, height));
    float relief = (0.84 + 0.18 * diffuse + 0.07 * sky) * contact;
    vec3 printed = mix(paper, albedo, 0.25) * relief;

    // Broad, nearly imperceptible press variation gives the sheet the tonal
    // richness of a bound atlas page without introducing a visible pattern.
    float atlasWash = fbm_detail(world * 0.83 + vec2(-31.0, 14.0));
    float pressVariation = sin(mapUv.x * 5.1 + atlasWash * 1.7)
                         * sin(mapUv.y * 4.3 - atlasWash * 1.3);
    printed *= 0.975 + atlasWash * 0.035 + pressVariation * 0.006;

    // Hairline contours read like engraved atlas marks, not modern terrain
    // shading. They remain deliberately faint at normal viewing distance.
    float contourDistance = abs(fract(height * 11.0) - 0.5);
    float contour = 1.0 - smoothstep(0.020, 0.055, contourDistance);
    printed *= 1.0 - contour * 0.045;

    float edge = smoothstep(0.44, 0.79, length((mapUv - 0.5) * vec2(1.0, 0.78)));
    printed *= 1.0 - edge * 0.045;
    outColor = vec4(printed, 1.0);
}
