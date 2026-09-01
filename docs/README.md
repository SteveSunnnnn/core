# Core documentation index

## Start here

- `ARCHITECTURE.md` — top-level authority: engine module ownership, the
  authority graph, runtime data flow, and what is wired vs still open.
- `FINAL_ENGINE_AUDIT.md` — verified engine build state and known boundaries.
- `ROADMAP.md` — planned capability work.
- `DETERMINISM.md` and `JOB_SYSTEM.md` — simulation determinism and threading.

## Runtime and content

- `SCRIPT_FIRST_CONTENT.md` — mandatory engine/content boundary, economy
  definition schema, content acceptance gate, and production font setup.
- `CORE_SCRIPT.md` — CoreScript language/runtime.
- `MOD_RUNTIME.md` — mod/content loading and deterministic load plans.
- `NOTIFICATION_RUNTIME.md` and `RESEARCH_ARCHITECTURE.md` — content runtimes.
- `SCRIPTED_GUI.md` — scripted GUI compiler and runtime boundary.
- `UI_THEME.md` — default UI theme, design tokens, typography and density.
- `ECONOMY_ARCHITECTURE.md` — economy data and tick model.
- `FINANCE_BANKING.md` — currency, FX, trade settlement, and bank ledgers.

## World and rendering

- `WORLD_COMPILER.md` — GIS/world-pack pipeline.
- `LIVING_MAP_ARCHITECTURE.md`, `POLITICAL_MAP_ARCHITECTURE.md`, and
  `MAP_MODE_ARCHITECTURE.md` — map data/runtime design.
- `TERRAIN_ARCHITECTURE.md`, `GPU_TIERS.md`, and `VULKAN_BACKEND.md` — rendering
  contracts.

## Status and performance

- `PERFORMANCE_BUDGET.md` and `PERFORMANCE_DESIGN.md` — current performance
  budgets.

Every doc below "Start here" is a subsystem contract and defers to
`ARCHITECTURE.md` on boundaries. When a historical benchmark conflicts with
current verification, `FINAL_ENGINE_AUDIT.md` is authoritative.

## Removed

Superseded overview documents deleted during the engine/game decoupling:
`ARCHITECTURE.md` (0.1 proposal, replaced in place), `ENGINE_ARCHITECTURE.md`,
`ENGINE_GAME_BOUNDARY_AUDIT.md`, `LOGIC_LAYER_1_0.md`, `RENDERING_TARGET.md`.
Their content now lives in `ARCHITECTURE.md`; milestone-era claims live in
`CHANGELOG.md`.
