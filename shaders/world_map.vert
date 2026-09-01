#version 460

// A page is tessellated in the vertex stage so the same streamable map tile
// can carry a shallow 3-D relief at close range and a quiet paper plane at
// distance. Adjacent pages use the same continuous height function, so no
// seam is introduced at a streaming boundary.
layout(location = 0) in vec4 mapRect;
layout(location = 0) out vec2 uv;
layout(location = 1) out float elevation;
layout(location = 2) out float closeFactor;

layout(set = 0, binding = 0) uniform sampler2D pageTexture;
layout(set = 0, binding = 1) uniform sampler2D heightTexture;

layout(push_constant) uniform MapView {
    vec4 view;   // center uv, half extents
    vec4 stream; // stream-window origin uv, span uv
    vec4 camera; // altitude metres, pitch degrees, aspect, reserved
};

// Enough vertices remain inside a 512-km page to show relief at the first
// close/medium clip level without multiplying the far-world draw excessively.
const int GRID = 32;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p) {
    vec2 cell = floor(p);
    vec2 f = fract(p);
    vec2 smoothF = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(cell), hash12(cell + vec2(1.0, 0.0)), smoothF.x),
               mix(hash12(cell + vec2(0.0, 1.0)), hash12(cell + vec2(1.0)), smoothF.x),
               smoothF.y);
}

vec2 map_uv(vec2 value) {
    return vec2(fract(value.x), clamp(value.y, 0.0, 1.0));
}

float terrainHeight(vec2 mapUv) {
    vec2 mapped = (map_uv(mapUv) - stream.xy) / max(stream.zw, vec2(1.0e-6));
    mapped.x = fract(mapped.x);
    mapped = clamp(mapped, vec2(0.0), vec2(1.0));
    vec4 encoded = textureLod(heightTexture, mapped, 0.0);
    // The page upload stores the quantised TerrainHeightPage value as
    // little-endian R16 in RG8. Water is encoded as zero metres; only land
    // above sea level contributes to the relief mesh.
    float metres = -12000.0 + (encoded.r * 255.0 + encoded.g * 65280.0) * 0.5;
    float sourceRelief = clamp(max(metres, 0.0) / 22000.0, 0.0, 1.0);
    // The political page is authoritative for land/water classification;
    // the alpha mask is a useful fast path but must not suppress relief when
    // a coarser clip page has a sparse spatial mask.
    vec4 politicalPage = textureLod(pageTexture, mapped, 0.0);
    float provinceId = politicalPage.r * 255.0 + politicalPage.g * 65280.0;
    float landMask = max(encoded.a, step(0.5, provinceId));

    // The checked-in world pack is intentionally usable without a bundled
    // DEM. Give that path a deterministic continuous relief fallback while
    // preserving authored DEM heights whenever they are present.
    vec2 p = (mapUv - vec2(0.5)) * vec2(34.0, 20.0);
    float broad = valueNoise(p * 0.20) * 0.62 + valueNoise(p * 0.46 + 17.0) * 0.38;
    float ridge = 1.0 - abs(valueNoise(p * 1.25 + 41.0) * 2.0 - 1.0);
    float fine = valueNoise(p * 3.4 - 13.0);
    float proceduralRelief = clamp(broad * 0.62 + ridge * ridge * 0.28 + fine * 0.10, 0.0, 1.0);
    return landMask > 0.5 ? max(sourceRelief, proceduralRelief * 0.72) : 0.0;
}

void main() {
    const vec2 corners[6] = vec2[](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
        vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));
    const int cell = gl_VertexIndex / 6;
    const vec2 gridCell = vec2(cell % GRID, cell / GRID);
    const vec2 local = (gridCell + corners[gl_VertexIndex % 6]) / float(GRID);
    uv = mapRect.xy + local * mapRect.zw;

    float altitude = max(camera.x, 350.0);
    closeFactor = 1.0 - smoothstep(70000.0, 1200000.0, altitude);
    float relief = terrainHeight(uv);
    // Mountains rise above the map only in the close/medium view. The far
    // atlas remains a coherent sheet instead of turning into a displaced grid.
    float reliefZ = relief * 0.62 * closeFactor;

    vec2 ndc = (uv - view.xy) / max(view.zw, vec2(1.0e-6));
    float pitch = radians(clamp(camera.y, 25.0, 88.0));
    float oblique = closeFactor;
    // Tilt the map plane toward the camera as the view approaches the
    // terrain. This is an actual low-oblique projection, not a 2-D colour
    // overlay: ground depth, height displacement, and perspective all share
    // the same camera transform.
    // Keep the projected world sheet covering the complete viewport. A pure
    // cos(pitch) scale would shrink the plane around its centre and expose
    // the dark clear colour as a false blue strip at the bottom of the map.
    // The mild overscan preserves the oblique read while eliminating that
    // artificial seam.
    float groundY = ndc.y * (1.0 + 0.24 * oblique);
    float screenY = groundY - reliefZ * sin(pitch) * 0.90;
    float depth = ndc.y * sin(pitch) * oblique - reliefZ * cos(pitch) * 0.42;
    float perspective = 1.0 / max(0.62, 1.0 + depth * (0.16 + 0.24 * closeFactor));

    gl_Position = vec4(ndc.x * perspective, screenY * perspective,
                        clamp(0.5 + depth, 0.0, 1.0), 1.0);
    elevation = relief;
}
