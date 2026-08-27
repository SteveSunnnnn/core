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
- [x] Three-year multi-worker deterministic continuation + periodic save/restore test.
- [x] Data-driven Event/Decision/Journal/AI Action content and persistent strategic `ai_plan` runtime.
- [x] AssetPack, ArchitectureKit and Strategy UI CPU-side draw/batch contracts.

### World / Living Map — implemented and under release gates
- [x] `.coreworld` random-access binary pack and strict metadata validation.
- [x] Province/coast, terrain, adjacency, spatial-mask, placement, settlement/resource/transport/river/lake/architecture-region chunk contracts.
- [x] GeoJSON/GeoPackage/Shapefile + optional DEM offline GIS compiler.
- [x] WorldBootstrap into runtime geography, adjacency and authored spatial placement.
- [x] Multi-chunk Living Map authored placement with deterministic fallback.
- [x] Terrain clipmap, political province-ID pages/coast SDF, picking, streaming and map modes.
- [x] Living Map compact GPU records and render-plan contracts.

### Renderer — RC validation foundation, not Final visual renderer
- [x] RenderGraph hazards and frame/resource lifetime model.
- [x] Vulkan 1.3 SDL3 device/surface/swapchain path.
- [x] 3 frames in flight, Synchronization2 and Dynamic Rendering.
- [x] Validation Layer + Debug Utils and machine-readable GPU report.
- [x] Windows one-command clean-build/shader/test/GPU validation script.
- [x] Terrain/Ocean/Political/Living/UI/MSDF shader contracts.
- [ ] Bind production terrain/ocean/political/Living/UI resources and pipelines in the live Vulkan backend.
- [ ] GPU-driven culling/indirect drawing, material/descriptor production path and residency integration.
- [ ] Final lighting, atmosphere, post-processing, water/coast and visual A/B QA against the target grand-strategy quality bar.
- [ ] RTX 4060 physical-machine Validation Layer gate and performance capture.

### Remaining logic/content work before Core 1.0 completion
- [ ] Production architecture/art kits for the first game (Britain first), authored by the game project rather than hard-coded in Core.
- [ ] Historical Capital 1836 world/content, balance and game-specific rules.
- [ ] Production-depth politics/diplomacy/war/economy systems, AI goal decomposition/negotiation/theatre planning and expanded CoreScript variables/collections/debug tooling.
- [ ] Game-specific advanced politics/diplomacy/war/economy behavior built on the generic engine primitives.

## Final promotion criteria

`1.0 Final` requires all CPU Release/ASan/UBSan/TSAN/stress/corruption/determinism gates plus the physical Windows GPU validation and live final-render pipeline/visual QA. RC-GPU must not be renamed Final solely because the swapchain validation run succeeds.
