#version 460

// The map pass is one continuous material. The page atlas supplies stable
// categorical geography; all paper, pigment and water variation is sampled in
// world-UV space so page/Location boundaries cannot restart the texture.
layout(location = 0) in vec2 uv;
layout(location = 1) in float elevation;
layout(location = 2) in float closeFactor;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D pageTexture;
layout(set = 0, binding = 1) uniform sampler2D heightTexture;
layout(set = 0, binding = 2) uniform sampler2D politicalPalette;

layout(push_constant) uniform Push {
    vec4 view;
    vec4 stream;
};

vec2 map_uv(vec2 value) {
    return vec2(fract(value.x), clamp(value.y, 0.0, 1.0));
}

vec2 stream_uv(vec2 value) {
    vec2 mapped = (map_uv(value) - stream.xy) / max(stream.zw, vec2(1.0e-6));
    mapped.x = fract(mapped.x);
    return clamp(mapped, vec2(0.0), vec2(1.0));
}

ivec2 page_coordinate(vec2 value) {
    ivec2 size = textureSize(pageTexture, 0);
    return clamp(ivec2(floor(stream_uv(value) * vec2(size))), ivec2(0), size - 1);
}

uvec4 page_bytes(ivec2 coordinate) {
    return uvec4(floor(texelFetch(pageTexture, coordinate, 0) * 255.0 + 0.5));
}

uint province_at(vec2 value) {
    uvec4 packed = page_bytes(page_coordinate(value));
    return packed.r | (packed.g << 8u);
}

int coast_at(vec2 value) {
    uvec4 packed = page_bytes(page_coordinate(value));
    uint bits = packed.b | (packed.a << 8u);
    return int(bits >= 32768u ? bits - 65536u : bits);
}

vec4 political_colour(uint provinceId) {
    if (provinceId == 0u) return vec4(0.0);
    ivec2 size = textureSize(politicalPalette, 0);
    uint capacity = uint(size.x * size.y);
    if (provinceId >= capacity) return vec4(0.0);
    ivec2 coordinate = ivec2(int(provinceId % uint(size.x)),
                             int(provinceId / uint(size.x)));
    return texelFetch(politicalPalette, coordinate, 0);
}

float height_at(vec2 value) {
    vec2 encoded = texture(heightTexture, stream_uv(value)).rg;
    return -12000.0 + (encoded.r * 255.0 + encoded.g * 65280.0) * 0.5;
}

float lake_at(vec2 value) {
    return texture(heightTexture, stream_uv(value)).b;
}

float spatial_land_at(vec2 value) {
    return texture(heightTexture, stream_uv(value)).a;
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float value_noise(vec2 p) {
    vec2 cell = floor(p);
    vec2 f = fract(p);
    vec2 smooth_f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(cell), hash12(cell + vec2(1.0, 0.0)), smooth_f.x),
               mix(hash12(cell + vec2(0.0, 1.0)), hash12(cell + vec2(1.0)), smooth_f.x),
               smooth_f.y);
}

float fbm(vec2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    mat2 rotate = mat2(0.80, -0.60, 0.60, 0.80);
    for (int octave = 0; octave < 5; ++octave) {
        value += amplitude * value_noise(p);
        p = rotate * p * 2.03 + 17.1;
        amplitude *= 0.48;
    }
    return value;
}

// Distance in page texels to a neighbouring sovereign colour. Sampling the
// palette, not the leaf ID, means internal Location boundaries do not become
// international borders after the three-level hierarchy is installed.
float sovereign_border_distance(vec2 value, uint centre_id, vec4 centre_colour) {
    ivec2 size = textureSize(pageTexture, 0);
    vec2 texel = 1.0 / vec2(size);
    const vec2 directions[8] = vec2[](
        vec2(1.0, 0.0), vec2(-1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, -1.0),
        vec2(0.707, 0.707), vec2(-0.707, 0.707),
        vec2(0.707, -0.707), vec2(-0.707, -0.707));
    float nearest = 18.0;
    for (int radius = 1; radius <= 16; radius *= 2) {
        for (int direction = 0; direction < 8; ++direction) {
            vec2 sample_uv = value + directions[direction] * texel * float(radius);
            uint neighbour_id = province_at(sample_uv);
            if (neighbour_id == 0u || neighbour_id == centre_id) continue;
            vec4 neighbour = political_colour(neighbour_id);
            if (distance(neighbour.rgb, centre_colour.rgb) > 0.025)
                nearest = min(nearest, float(radius));
        }
    }
    return nearest;
}

float location_border_distance(vec2 value, uint centre_id) {
    if (centre_id == 0u) return 8.0;
    ivec2 size = textureSize(pageTexture, 0);
    vec2 texel = 1.0 / vec2(size);
    const vec2 directions[4] = vec2[](
        vec2(1.0, 0.0), vec2(-1.0, 0.0),
        vec2(0.0, 1.0), vec2(0.0, -1.0));
    float nearest = 8.0;
    for (int radius = 1; radius <= 4; radius *= 2) {
        for (int direction = 0; direction < 4; ++direction) {
            uint neighbour_id = province_at(value + directions[direction] * texel * float(radius));
            if (neighbour_id != 0u && neighbour_id != centre_id)
                nearest = min(nearest, float(radius));
        }
    }
    return nearest;
}

vec3 srgb_to_linear(vec3 value) {
    return pow(max(value, vec3(0.0)), vec3(2.2));
}

void main() {
    vec2 mapUv = map_uv(uv);
    uint centre_id = province_at(mapUv);
    vec4 political = political_colour(centre_id);
    float lake = lake_at(mapUv);
    float land = (centre_id != 0u && lake < 0.5 && spatial_land_at(mapUv) > 0.5) ? 1.0 : 0.0;

    // Stable geographic coordinates: broad masses, medium mottle and fine
    // paper grain are all continuous across pages and political ownership.
    vec2 world = (mapUv - vec2(0.5)) * vec2(34.0, 18.0);
    float broad = fbm(world * 0.18 + vec2(4.0, -7.0));
    float medium = fbm(world * 0.72 + vec2(-19.0, 13.0));
    float paper_grain = value_noise(world * 8.5 + vec2(21.0, 5.0)) - 0.5;
    float fibers = sin(dot(world, vec2(5.7, 0.41)) + broad * 4.0) * 0.5 + 0.5;

    vec3 paper = vec3(0.76, 0.745, 0.705);
    paper *= 0.975 + broad * 0.035 + medium * 0.018;
    paper += paper_grain * 0.010 + (fibers - 0.5) * 0.003;

    // The requested ocean palette is intentionally pale blue-grey rather than
    // saturated seawater. Its variation is 800-3000 km scale plus restrained
    // regional mottle, never a per-page tile.
    float ocean_wash = fbm(world * 0.055 + vec2(-31.0, 18.0));
    float ocean_mottle = fbm(world * 0.22 + vec2(11.0, -24.0));
    vec3 ocean = vec3(0.43, 0.56, 0.58);
    ocean = mix(ocean, vec3(0.52, 0.63, 0.64), ocean_wash * 0.38);
    ocean *= 0.985 + ocean_mottle * 0.025 + paper_grain * 0.012;

    vec3 terrain = mix(vec3(0.48, 0.48, 0.40), vec3(0.68, 0.63, 0.51),
                       smoothstep(0.35, 0.85, elevation));
    float dx = dFdx(elevation);
    float dy = dFdy(elevation);
    vec3 normal = normalize(vec3(-dx * 16.0, -dy * 16.0, 1.0));
    float light = 0.80 + 0.14 * max(dot(normal, normalize(vec3(-0.40, -0.34, 0.82))), 0.0);
    // Match the vertex relief field at pixel scale so the close view reads as
    // lit terrain instead of a flat political fill, even when the pack has no
    // optional DEM payload.
    float topo_field = fbm(world * 0.52 + vec2(7.0, -11.0)) * 0.72 +
                       fbm(world * 1.65 + vec2(-23.0, 19.0)) * 0.28;
    vec3 topo_normal = normalize(vec3(-dFdx(topo_field) * 24.0,
                                      -dFdy(topo_field) * 24.0, 1.0));
    float topo_light = 0.68 + 0.46 * max(dot(topo_normal,
                                               normalize(vec3(-0.48, -0.34, 0.80))), 0.0);
    terrain *= light;

    vec3 pigment = srgb_to_linear(political.rgb);
    float border_distance = sovereign_border_distance(mapUv, centre_id, political);
    float broad_edge = 1.0 - smoothstep(1.0, 15.0, border_distance);
    // Prevent small countries from becoming black blobs: edge pigment is
    // bounded and the interior never falls below a readable paper tint.
    float pigment_strength = 0.33 + broad_edge * 0.17;
    pigment_strength *= 0.92 + broad * 0.10 + medium * 0.06 + paper_grain * 0.025;
    vec3 land_colour = mix(paper, pigment, pigment_strength);
    land_colour = mix(land_colour, terrain, closeFactor * 0.42);
    land_colour *= light;
    land_colour *= mix(1.0, topo_light, closeFactor * 0.90);
    float topo_contour = 1.0 - smoothstep(0.025, 0.075,
                                           abs(fract(topo_field * 9.0) - 0.5));
    land_colour *= 1.0 - topo_contour * closeFactor * 0.055;

    vec3 close_ocean = vec3(0.075, 0.19, 0.24) + vec3(0.02, 0.035, 0.04) * ocean_mottle;
    vec3 map_colour = mix(close_ocean, land_colour, land);
    map_colour = mix(map_colour, mix(ocean, land_colour, land), 1.0 - closeFactor);

    // Country shadow + thin carbon core; coastline gets only a land-side
    // shadow and a pale cyan edge, never a black sovereign outline.
    float country_shadow = (1.0 - smoothstep(1.0, 10.0, border_distance)) * land;
    float country_line = (1.0 - smoothstep(0.55, 1.35, border_distance)) * land;
    map_colour = mix(map_colour, map_colour * vec3(0.90, 0.885, 0.86), country_shadow * 0.30);
    map_colour = mix(map_colour, vec3(0.16, 0.145, 0.13), country_line * 0.70);

    // Leaf/location boundaries stay in the same page atlas as political
    // colour, so close zoom has a usable parcel hairline without a CPU walk
    // over the entire near-vector file during a drag.
    float location_distance = location_border_distance(mapUv, centre_id);
    float location_line = (1.0 - smoothstep(0.65, 1.45, location_distance)) * land * closeFactor * 0.42;
    map_colour = mix(map_colour, vec3(0.28, 0.265, 0.24), location_line);

    float coast_distance = abs(float(coast_at(mapUv))) * 0.5;
    float coast_width = max(fwidth(coast_distance) * 2.0, 2.0);
    float coast_edge = 1.0 - smoothstep(0.0, coast_width, coast_distance);
    float land_coast_shadow = coast_edge * land * 0.28;
    map_colour *= 1.0 - land_coast_shadow;
    map_colour = mix(map_colour, vec3(0.45, 0.61, 0.61), coast_edge * (1.0 - land) * 0.14);

    // Close terrain is more legible; far paper remains subdued and clean.
    map_colour *= 1.0 + (paper_grain - 0.5) * 0.018 * (0.4 + closeFactor);
    outColor = vec4(max(map_colour, vec3(0.0)), 1.0);
}
