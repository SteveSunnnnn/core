# Core Architecture 0.1

## Mission

Core exists to run one class of game extremely well: a global grand-strategy simulation with tens or hundreds of thousands of simulation cohorts, thousands of geographic regions, dense economic dependencies, high-frequency UI queries, and a continuously zoomable 3D map.

## Architectural laws

1. **Simulation never depends on rendering.** The renderer consumes snapshots; it cannot mutate the world.
2. **Player/UI actions become deterministic Commands.** This enables replay, multiplayer lockstep experiments, debugging and OOS detection.
3. **Runtime data is data-oriented.** Stable typed IDs index dense SoA stores. Avoid one heap object per country/pop/building.
4. **Content is data, not C++.** Countries, buildings, laws, technology, events, AI weights and most rules must be externally definable.
5. **Expensive derived state is reactive.** Modifier/dependency nodes become dirty and are recomputed only when required.
6. **Simulation work is expressed as a dependency DAG.** Tasks state frequency, prerequisites and estimated cost; a job system will parallelize safe batches.
7. **Determinism is a feature.** RNG streams are named/seeded, commands are sequenced, world checksums are available, and non-deterministic iteration is prohibited in simulation code.
8. **The world compiler does expensive GIS work offline.** Runtime never parses raw shapefiles/GeoJSON for the shipping map.
9. **Mods are first-class.** Overlay load order, namespaces, validation, replace/extend semantics, scripted triggers/effects/values/modifiers, localization and error reporting are engine features.
10. **No generic-engine tax.** Features that do not directly serve grand strategy are not built unless a shipping game requires them.

## Major modules

```text
Core
├── Foundation
│   ├── allocator / arenas (later)
│   ├── strong IDs
│   ├── deterministic RNG
│   ├── hashing/checksums
│   ├── logging / diagnostics
│   └── job system
│
├── World Runtime
│   ├── countries / states / provinces
│   ├── pops
│   ├── buildings
│   ├── markets / goods
│   ├── diplomacy
│   └── military
│
├── Simulation Kernel
│   ├── GameClock
│   ├── CommandQueue
│   ├── TickTask DAG
│   ├── ModifierGraph
│   ├── event queue
│   └── deterministic replay
│
├── CoreScript
│   ├── parser + AST
│   ├── typed scopes
│   ├── triggers
│   ├── effects
│   ├── scripted values
│   ├── modifiers
│   ├── iterators / scope switching
│   └── bytecode VM / compiled IR
│
├── Content / Mod Runtime
│   ├── overlay VFS
│   ├── definition database
│   ├── history application
│   ├── localization
│   ├── validation
│   └── hot reload (editor/dev)
│
├── AI Runtime
│   ├── candidate generation in C++
│   ├── scripted eligibility
│   ├── scripted utility weights
│   ├── deterministic semi-random choice
│   └── strategic plans
│
├── Renderer
│   ├── Vulkan 1.4 backend
│   ├── render graph
│   ├── GPU-driven terrain
│   ├── ocean/coast SDF
│   ├── political/province ID surfaces
│   ├── border renderer
│   ├── GPU instance clusters
│   ├── roads/rail splines
│   ├── labels
│   ├── map lenses
│   └── atmosphere/post
│
├── UI
│   ├── retained-mode layout
│   ├── declarative UI documents
│   ├── data bindings
│   ├── scripted visibility/enabled state
│   ├── virtualization for huge lists
│   └── localization-aware text
│
└── Toolchain
    ├── WorldCompiler (GIS -> shipping binaries)
    ├── AssetCooker (glTF/KTX2/meshlets/LOD)
    ├── ContentValidator
    ├── ScriptCompiler
    ├── Profiler
    └── Map/Content editor
```

## Jomini-class capability mapping

Core aims for equivalent *categories of capability*, implemented independently:

| Capability | Core design |
|---|---|
| Data-defined countries/history | Definition DB + history pass |
| Scope-aware scripting | `ScopeType` + compiled CoreScript |
| Triggers/effects | Native registry + script IR |
| Scripted GUI hooks | UI bindings evaluate read-only scopes; writes emit Commands |
| Mod replace/extend | Overlay VFS + per-database merge policies |
| Moddable AI | native candidate loop + scripted eligibility/weights |
| Modifiers | dependency graph + dirty propagation + batched recompute |
| Simulation ticks | frequency-aware TickTask DAG |
| Performance introspection | per-task timings + dependency graph viewer |
| Save/OOS | binary snapshot + deterministic command journal + checksums |
| Map modes/lenses | GPU data textures/buffers fed from render snapshots |

## Simulation/renderer synchronization

The renderer never locks hot world arrays. The simulation publishes a read-only RenderSnapshot at controlled synchronization points. Later this becomes triple-buffered and incrementally patched.

```text
Simulation Thread(s) -> World State -> Snapshot Builder -> Render Snapshot
                                                     -> Vulkan Renderer
```

This avoids making tick speed depend directly on map rendering work.
