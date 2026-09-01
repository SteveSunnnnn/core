# Core Grand Strategy Engine 1.0

Core is a data-driven, deterministic C++23 engine for large historical and
grand-strategy games. The current repository is the **Core 1.0 Development
Preview**: its versioned runtime and save line remain 1.0.0, while public API
and advanced central-bank/interbank simulation continue to mature.

Core is an independent engine implementation. It does not contain or require
proprietary Clausewitz, Jomini, or commercial-game source code. Its engine
documentation records capability boundaries; it is not a claim of drop-in parity.

## What is implemented

- Deterministic fixed-step simulation, typed IDs, SoA stores, JobSystem worker
  pool, stable reductions, replay checkpoints, and desync checks.
- CoreScript parser/compiler/bytecode VM with scopes, variables, collections,
  event targets, typed arguments, scripted triggers/effects/values, and
  execution limits.
- Data-driven events, decisions, journals, notifications, on-actions, research,
  localization, mod load plans, and scripted GUI compilation.
- Market/building/POP economy with fixed-point hot paths, market inventory,
  fulfillment, multi-currency settlement, FX and metallic standards, authored
  trade routes, tariffs/logistics capacity, regulated commercial banks,
  persistent loans/defaults, sovereign credit, construction, save support, and
  explained weekly monetary auditing.
- Operational foundations for politics, diplomacy, strategic AI, warfare,
  logistics, migration, technology, companies, ownership, and construction.
- World-pack/GIS tooling, geography and adjacency stores, hierarchical
  pathfinding, living-map streaming, vector cartography, terrain/water/VFX,
  GPU-driven render planning, strategy UI, tooltips, HUD, and map editing.
- Atomic save decoding with legacy migrations, asynchronous save writing,
  deterministic checksums, and malformed-input validation.

For verified behavior and known boundaries, read
[`docs/FINAL_ENGINE_AUDIT.md`](docs/FINAL_ENGINE_AUDIT.md). The engine module
boundaries are described in
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## Requirements

Headless engine:

- CMake 3.25 or newer;
- Ninja;
- a C++23 compiler (recent MSVC, GCC, or Clang);
- optional Zstandard and xxHash development packages for compressed world
  packs and accelerated integrity checks.

## Build

Debug headless:

```powershell
cmake --preset dev-headless
cmake --build --preset dev-headless -j 8
```

Release headless:

```powershell
cmake --preset release-headless
cmake --build --preset release-headless -j 8
```

The repository ships the reusable engine runtime and its engine-side tooling.
Linux CPU CI and Windows headless CI build the public engine tree.

## Repository layout

```text
Core/
├── .github/workflows/     Continuous integration
├── docs/                  Architecture, runtime contracts, audits, roadmap
├── shaders/               GLSL shader contracts and sources
├── src/apps/               Engine CLI, world compiler, inspect, and cookers
├── src/core/               Engine runtime modules
│   ├── ai/ economy/ gameplay/ grand_strategy/ research/ warfare/
│   ├── assets/ content/ localization/ save/ scripting/
│   ├── render/ ui/ platform/ editor/
│   └── base/ jobs/ memory/ simulation/ world/ worldpack/
├── thirdparty/             Vendored third-party source/header distributions
└── tools/                  GIS, asset, shader, and Windows engine tooling
```

The retained UI state machine (`src/core/ui/ScriptedGuiRuntime.cpp`) is
compiled into `core_runtime` and verified against typed data provider bindings
and virtualized collections.

## Contributing and security

See [`CONTRIBUTING.md`](CONTRIBUTING.md) for deterministic-state requirements.
Security reports should follow
[`SECURITY.md`](SECURITY.md).

## License

Core is licensed under the [MIT License](LICENSE). Vendored dependencies and
derived technical data retain their own licenses and attribution; see
[`THIRD_PARTY.md`](THIRD_PARTY.md).
