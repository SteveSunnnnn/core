# Core Renderer Target

## First proof target

Do **not** build the globe first. The renderer must first prove final-quality visual capability on a Britain vertical slice (London–Liverpool–Manchester, surrounding countryside and coasts).

## Target stack

- C++23
- Vulkan 1.4 baseline
- SDL3 for platform/window/input only
- SPIR-V shaders
- GPU-driven instancing and indirect draws
- glTF 2.0 asset interchange
- KTX2/BasisU texture packaging
- meshoptimizer-compatible cooked geometry

## Frame architecture target

```text
CPU simulation snapshot
  -> visibility / streaming requests
  -> GPU scene buffers
  -> depth/Hi-Z
  -> terrain + water
  -> clustered instances
  -> roads/rails
  -> labels/map overlays
  -> atmosphere/clouds
  -> post/AA
  -> UI
```

## Visual quality gates before global rollout

1. Terrain silhouette and material blending survive 3 zoom levels.
2. Coastal shallow-water gradient and foam are stable without pixel crawl.
3. Forest density reads naturally from strategic camera angles.
4. Settlement generator creates coherent British industrial/rural morphology.
5. Rail, road and port infrastructure visually follows simulation state.
6. Political tint fades smoothly into physical terrain toward close zoom.
7. Country/province borders remain anti-aliased and stable at all map zooms.
8. 1440p target frame time <= 16.6 ms on the agreed reference GPU for the slice.
