#version 460

// Layered grand-strategy map. The far camera is an administrative paper map;
// the close camera is terrain-led. Province IDs remain categorical while
// terrain and height use filtered sampling, so political data never turns into
// a blurred colour screenshot.

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D provinceIds;
layout(set = 0, binding = 1) uniform sampler2D terrainTexture;
layout(set = 0, binding = 2) uniform sampler2D heightTexture;
layout(set = 0, binding = 3) uniform sampler2D politicalLut;

layout(push_constant) uniform Push {
    vec4 view;   // center uv, half extents
    vec4 params; // reserved for selectable map modes
} pc;

vec2 map_uv(vec2 value) {
    return vec2(fract(value.x), clamp(value.y, 0.0, 1.0));
}

uint decode_location(vec4 packed) {
    uvec3 bytes = uvec3(floor(packed.rgb * 255.0 + 0.5));
    return bytes.r | (bytes.g << 8u) | (bytes.b << 16u);
}

ivec2 id_coordinate(vec2 value) {
    ivec2 size = textureSize(provinceIds, 0);
    vec2 wrapped = map_uv(value);
    return ivec2(clamp(ivec2(floor(wrapped * vec2(size))), ivec2(0), size - 1));
}

uint location_at(vec2 value) {
    return decode_location(texelFetch(provinceIds, id_coordinate(value), 0));
}

vec4 political_colour(uint locationId) {
    if (locationId == 0u) return vec4(0.0);
    ivec2 size = textureSize(politicalLut, 0);
    uint capacity = uint(size.x * size.y);
    if (locationId >= capacity) return vec4(0.0);
    ivec2 coordinate = ivec2(int(locationId % uint(size.x)),
                             int(locationId / uint(size.x)));
    return texelFetch(politicalLut, coordinate, 0);
}

float height_at(vec2 value) {
    vec2 encoded = texture(heightTexture, map_uv(value)).rg;
    return dot(encoded, vec2(255.0, 65280.0)) / 65535.0;
}

vec3 srgb_to_linear(vec3 value) {
    return pow(max(value, vec3(0.0)), vec3(2.2));
}

float luminance(vec3 value) {
    return dot(value, vec3(0.2126, 0.7152, 0.0722));
}

float hash12(vec2 value) {
    vec3 p = fract(vec3(value.xyx) * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float grid_line(float coordinate) {
    float distanceToLine = min(fract(coordinate), 1.0 - fract(coordinate));
    float antialias = max(fwidth(coordinate), 0.001);
    return 1.0 - smoothstep(0.006, 0.006 + antialias, distanceToLine);
}

void main() {
    vec2 mapUv = map_uv(uv);
    ivec2 idSize = textureSize(provinceIds, 0);
    vec2 idTexel = 1.0 / vec2(idSize);

    // Four categorical samples provide coverage only at the edge. IDs are
    // never interpolated into invented province numbers.
    vec2 pixel = mapUv * vec2(idSize) - 0.5;
    vec2 fraction = fract(pixel);
    ivec2 base = ivec2(floor(pixel));
    ivec2 p00 = clamp(base, ivec2(0), idSize - 1);
    ivec2 p10 = clamp(base + ivec2(1, 0), ivec2(0), idSize - 1);
    ivec2 p01 = clamp(base + ivec2(0, 1), ivec2(0), idSize - 1);
    ivec2 p11 = clamp(base + ivec2(1, 1), ivec2(0), idSize - 1);
    uint id00 = decode_location(texelFetch(provinceIds, p00, 0));
    uint id10 = decode_location(texelFetch(provinceIds, p10, 0));
    uint id01 = decode_location(texelFetch(provinceIds, p01, 0));
    uint id11 = decode_location(texelFetch(provinceIds, p11, 0));

    float l00 = id00 != 0u ? 1.0 : 0.0;
    float l10 = id10 != 0u ? 1.0 : 0.0;
    float l01 = id01 != 0u ? 1.0 : 0.0;
    float l11 = id11 != 0u ? 1.0 : 0.0;
    float rawLand = mix(mix(l00, l10, fraction.x),
                        mix(l01, l11, fraction.x), fraction.y);
    float landCoverage = smoothstep(0.20, 0.80, rawLand);

    vec4 c00 = political_colour(id00);
    vec4 c10 = political_colour(id10);
    vec4 c01 = political_colour(id01);
    vec4 c11 = political_colour(id11);
    float w00 = (1.0 - fraction.x) * (1.0 - fraction.y) * l00;
    float w10 = fraction.x * (1.0 - fraction.y) * l10;
    float w01 = (1.0 - fraction.x) * fraction.y * l01;
    float w11 = fraction.x * fraction.y * l11;
    float politicalWeight = w00 + w10 + w01 + w11;
    vec4 political = vec4(0.0);
    if (politicalWeight > 0.0001) {
        political = (c00 * w00 + c10 * w10 + c01 * w01 + c11 * w11) /
                    politicalWeight;
    }

    // Zoom hierarchy: theatre/world scales become a calm paper map; closer
    // scales reveal terrain and progressively reduce political paint.
    float paperBlend = smoothstep(0.065, 0.145, pc.view.z);
    float terrainBlend = 1.0 - paperBlend;
    float localDetail = 1.0 - smoothstep(0.040, 0.105, pc.view.z);

    vec3 sourceTerrain = srgb_to_linear(texture(terrainTexture, mapUv).rgb);
    float sourceLuma = luminance(sourceTerrain);
    // Colourless relief: the land base carries only luminance so political
    // ownership paint stays the visual lead; the 3D module supplies its own
    // surface colour later.
    vec3 terrainColour = mix(vec3(sourceLuma), sourceTerrain, 0.08);
    terrainColour = mix(terrainColour, vec3(0.30, 0.31, 0.23), 0.055);

    vec2 heightStep = max(idTexel, fwidth(mapUv) * 0.75);
    float hLeft = height_at(mapUv - vec2(heightStep.x, 0.0));
    float hRight = height_at(mapUv + vec2(heightStep.x, 0.0));
    float hUp = height_at(mapUv - vec2(0.0, heightStep.y));
    float hDown = height_at(mapUv + vec2(0.0, heightStep.y));
    vec3 normal = normalize(vec3((hLeft - hRight) * mix(2.0, 8.0, terrainBlend),
                                 (hUp - hDown) * mix(2.0, 8.0, terrainBlend), 1.0));
    vec3 lightDirection = normalize(vec3(-0.42, -0.54, 0.73));
    float hillLight = 0.76 + max(dot(normal, lightDirection), 0.0) * 0.30;
    // A single stable grain lookup replaces the previous two four-octave FBM
    // stacks. The authored terrain and mip chain already carry large-scale
    // variation; this only prevents the close view from feeling sterile.
    vec2 grainScale = mix(vec2(520.0, 320.0), vec2(2100.0, 1280.0), localDetail);
    float surfaceGrain = hash12(floor(mapUv * grainScale));
    terrainColour *= hillLight * (1.0 + (surfaceGrain - 0.50) * 0.055 * localDetail);

    vec3 politicalLinear = srgb_to_linear(political.rgb);
    float politicalLuma = luminance(politicalLinear);
    vec3 mutedPolitical = mix(vec3(politicalLuma), politicalLinear, 0.85);

    // Close map: political ownership leads over the colourless relief.
    float closePoliticalOpacity = mix(0.30, 0.48, smoothstep(0.035, 0.11, pc.view.z)) * political.a;
    vec3 closeLand = mix(terrainColour, mutedPolitical, closePoliticalOpacity);
    vec3 closeOcean = vec3(0.075, 0.235, 0.315);
    closeOcean += vec3(0.035, 0.055, 0.060) * (sourceLuma - 0.35);
    closeOcean *= 0.988 + (surfaceGrain - 0.5) * 0.018 * localDetail;
    vec3 closeMap = mix(closeOcean, closeLand, landCoverage);

    // Far map: warm paper, low-chroma country washes and pale blue-grey seas.
    vec3 paperLandBase = vec3(0.705, 0.680, 0.615) *
                         (0.990 + (surfaceGrain - 0.5) * 0.018);
    vec3 pastelPolitical = mix(paperLandBase, mutedPolitical, 0.62);
    vec3 paperLand = mix(paperLandBase, pastelPolitical, 0.88 * political.a);
    paperLand *= mix(1.0, hillLight, 0.10);

    vec3 paperOcean = vec3(0.610, 0.705, 0.735) *
                      (0.994 + (surfaceGrain - 0.5) * 0.012);
    float atlasGrid = max(grid_line(mapUv.x * 24.0), grid_line(mapUv.y * 12.0));
    paperOcean = mix(paperOcean, vec3(0.42, 0.52, 0.57), atlasGrid * 0.075);
    vec3 paperMap = mix(paperOcean, paperLand, landCoverage);

    vec3 mapColour = mix(closeMap, paperMap, paperBlend);

    // Coastline coverage is independent of political colour and remains stable
    // through the paper/terrain transition.
    float coastBand = 4.0 * landCoverage * (1.0 - landCoverage);
    mapColour = mix(mapColour,
                    mix(vec3(0.055, 0.105, 0.125), vec3(0.24, 0.25, 0.23), paperBlend),
                    coastBand * 0.34);

    // Screen-footprint categorical neighbours produce constant-pixel-width
    // borders without uploading hundreds of thousands of CPU line vertices.
    vec2 footprint = max(fwidth(mapUv) * 1.20, idTexel);
    uint centreId = location_at(mapUv);
    vec4 centreColour = political_colour(centreId);
    uint eastId = location_at(mapUv + vec2(footprint.x, 0.0));
    uint westId = location_at(mapUv - vec2(footprint.x, 0.0));
    uint southId = location_at(mapUv + vec2(0.0, footprint.y));
    uint northId = location_at(mapUv - vec2(0.0, footprint.y));
    uint neighbours[4] = uint[4](eastId, westId, southId, northId);
    float countryEdge = 0.0;
    float provinceEdge = 0.0;
    for (int index = 0; index < 4; ++index) {
        uint neighbourId = neighbours[index];
        if (neighbourId == centreId || neighbourId == 0u || centreId == 0u) continue;
        provinceEdge = 1.0;
        vec4 neighbourColour = political_colour(neighbourId);
        if (abs(neighbourColour.a - centreColour.a) > 0.25 ||
            distance(neighbourColour.rgb, centreColour.rgb) > 0.035) {
            countryEdge = 1.0;
        }
    }
    float provinceVisibility = localDetail * (1.0 - countryEdge) * 0.11;
    mapColour = mix(mapColour, vec3(0.13, 0.12, 0.105),
                    provinceEdge * provinceVisibility);
    mapColour = mix(mapColour, vec3(0.095, 0.085, 0.073),
                    countryEdge * mix(0.30, 0.52, paperBlend));

    outColor = vec4(max(mapColour, vec3(0.0)), 1.0);
}
