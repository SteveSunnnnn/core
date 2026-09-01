# Core Roadmap

## Core 1.0 Development — Logic Foundation R2 — current

### CPU / simulation / data — implemented and under release gates
- [x] C++23 deterministic runtime, typed IDs and SoA domain stores.
- [x] Deterministic multi-core JobSystem, stable reductions/RNG/command stages.
- [x] Tick DAG, modifiers, command validation, world checksums.
- [x] CoreScript 2.0 foundation: multi-scope traversal, ROOT/FROM/PREV/THIS, saved scopes, deterministic iterators, symbolic arguments and content/mod/localization hashes.
- [x] POP/building/market economy with Production Methods and demographic/political POP columns.
- [x] Geography Country/Market/State/Province runtime and CSR scope/adjacency.
- [x] Generic grand-strategy stores plus operational first-pass government/law, diplomacy/play and war/front/battle state machines.
- [x] Atomic Save/Load with stable-key Gameplay/AI runtime state, schema-v1 migration, ReplayJournal, CoreEngine lifecycle and corruption/reference validation.
- [x] Three-year multi-worker deterministic continuation + periodic save/restore validation.
- [x] Data-driven Event/Decision/Journal/AI Action content and persistent strategic `ai_plan` runtime.
- [x] AssetPack, ArchitectureKit and Strategy UI CPU-side draw/batch contracts.

### World / Living Map — implemented and under release gates
- [x] `.coreworld` random-access binary pack and strict metadata validation.
- [x] Pure `WorldTopology` pack decode separated from `WorldBootstrap`/economy
  composition.
- [x] Province/coast, terrain, adjacency, spatial-mask, placement, settlement/resource/transport/river/lake/architecture-region chunk contracts.
- [x] GeoJSON/GeoPackage/Shapefile + optional DEM offline GIS compiler.
- [x] WorldBootstrap into runtime geography, adjacency and authored spatial placement.
- [x] Multi-chunk Living Map authored placement with deterministic fallback.
- [x] Terrain clipmap, political province-ID pages/coast SDF, picking, streaming and map modes.
- [x] World-map page payload/source/cache/planner/streamer are map-owned; the
  Vulkan backend consumes only bounded atlas upload intents.
- [x] Living Map compact GPU records and render-plan contracts.

### Renderer — engine validation foundation
- [x] RenderGraph hazards and frame/resource lifetime model.
- [x] Vulkan 1.3 device and render-resource contracts.
- [x] 3 frames in flight, Synchronization2 and Dynamic Rendering.
- [x] Validation-layer contracts and machine-readable diagnostics.
- [x] Windows one-command engine/shader validation script.
- [x] Terrain/Ocean/Political/Living/UI/MSDF shader contracts.
- [x] Implemented `.coreworld` political page atlas, height/mask upload path
  and script-owned ownership palette in the renderer contracts.
- [ ] Bind the remaining production terrain/ocean/Living/UI resources and
  pipelines in the live Vulkan backend.
- [ ] GPU-driven culling/indirect drawing, material/descriptor production path and residency integration.
- [ ] Final lighting, atmosphere, post-processing, water/coast and visual QA in
  an external host application.
- [ ] Physical-machine Validation Layer gate and performance capture.

### Remaining engine work before Core 1.0 completion
- [ ] Production-depth politics/diplomacy/war/economy systems, AI goal decomposition,
  negotiation/theatre planning and expanded CoreScript variables/collections/debug tooling.
- [ ] External host integration for authored content, rendering and presentation.

## Final promotion criteria

`1.0 Final` requires all CPU Release/ASan/UBSan/TSAN/stress/corruption/determinism gates plus the physical Windows GPU validation and live final-render pipeline/visual QA. RC-GPU must not be renamed Final solely because the swapchain validation run succeeds.
