# Core Grand Strategy Engine 1.0

Core is a data-driven, deterministic C++23 engine for large historical and
grand-strategy games. The current repository is the **Core 1.0 Development
Preview**: its versioned runtime and save line remain 1.0.0, while public API,
content breadth, desktop presentation, and full banking/FX simulation continue
to mature.

Core is an independent engine implementation. It does not contain or require
proprietary Clausewitz, Jomini, or commercial-game source code. Compatibility
research in this repository documents observable behavior and capability gaps;
it is not a claim of drop-in parity.

## What is implemented

- Deterministic fixed-step simulation, typed IDs, SoA stores, JobSystem worker
  pool, stable reductions, replay checkpoints, and desync checks.
- CoreScript parser/compiler/bytecode VM with scopes, variables, collections,
  event targets, typed arguments, scripted triggers/effects/values, and
  execution limits.
- Data-driven events, decisions, journals, notifications, on-actions, research,
  localization, mod load plans, and scripted GUI compilation.
- Market/building/POP economy with fixed-point hot paths, market inventory,
  fulfillment, same-currency settlement accounts, taxation, investment, trade,
  construction, save support, and weekly monetary-conservation auditing.
- Operational foundations for politics, diplomacy, strategic AI, warfare,
  logistics, migration, technology, companies, ownership, and construction.
- World-pack/GIS tooling, geography and adjacency stores, hierarchical
  pathfinding, living-map streaming, vector cartography, terrain/water/VFX,
  GPU-driven render planning, strategy UI, tooltips, HUD, and map editing.
- Atomic save decoding with legacy migrations, asynchronous save writing,
  deterministic checksums, and malformed-input regression coverage.

For verified behavior and known boundaries, read
[`docs/FINAL_ENGINE_AUDIT.md`](docs/FINAL_ENGINE_AUDIT.md). The broader
capability comparison is maintained in
[`docs/VIC3_JOMINI_GAP_MATRIX.md`](docs/VIC3_JOMINI_GAP_MATRIX.md).

## Requirements

Headless engine and tests:

- CMake 3.25 or newer;
- Ninja;
- a C++23 compiler (recent MSVC, GCC, or Clang);
- optional Zstandard and xxHash development packages for compressed world
  packs and accelerated integrity checks.

Desktop client additionally requires:

- Vulkan SDK 1.3 or newer, including headers and the loader import library;
- a Vulkan 1.3-capable driver/device;
- SDL 3.4.14, fetched by CMake when `CORE_BUILD_DESKTOP=ON`.

## Build and test

Debug headless:

```powershell
cmake --preset dev-headless
cmake --build --preset dev-headless -j 8
ctest --preset dev-headless --output-on-failure
```

Release headless QA and benchmarks:

```powershell
cmake --preset release-headless
cmake --build --preset release-headless -j 8
ctest --preset release-headless --output-on-failure

.\build\release-headless\core_economy_bench.exe
.\build\release-headless\core_living_bench.exe
.\build\release-headless\core_bench.exe
```

Desktop:

```powershell
cmake --preset release-desktop
cmake --build --preset release-desktop -j 8
```

The repository currently contains 26 registered regression suites. Linux CPU
CI and Windows headless CI build and test the public tree; the physical Vulkan
validation gate remains a target-machine release check.

## Repository layout

```text
Core/
├── .github/workflows/     Continuous integration
├── bench/                 Deterministic performance baselines
├── content/               Data-driven example/base content
├── demo/                  Technical demo documentation and generated inputs
├── docs/                  Architecture, runtime contracts, audits, roadmap
├── shaders/               GLSL shader contracts and sources
├── src/apps/               CLI, demos, cookers, desktop entry point
├── src/core/               Engine runtime modules
│   ├── ai/ economy/ gameplay/ grand_strategy/ research/ warfare/
│   ├── assets/ content/ localization/ save/ scripting/
│   ├── render/ ui/ platform/ editor/
│   └── base/ jobs/ memory/ simulation/ world/ worldpack/
├── tests/                  Unit, regression, determinism, save, stress tests
├── thirdparty/             Vendored third-party source/header distributions
└── tools/                  GIS, Blender, shader and Windows validation tools
```

The retained UI state machine (`src/core/ui/ScriptedGuiRuntime.cpp`) is
compiled into `core_runtime` and verified against typed data provider bindings
and virtualized collections.

## Contributing and security

See [`CONTRIBUTING.md`](CONTRIBUTING.md) for deterministic-state requirements
and test expectations. Security reports should follow
[`SECURITY.md`](SECURITY.md).

## License

Core is licensed under the [MIT License](LICENSE). Vendored dependencies and
derived technical data retain their own licenses and attribution; see
[`THIRD_PARTY.md`](THIRD_PARTY.md).
