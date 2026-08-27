# Core documentation index

## Start here

- `FINAL_ENGINE_AUDIT.md` — verified build/test state and known boundaries.
- `ARCHITECTURE.md` — top-level engine architecture.
- `ROADMAP.md` — planned capability work.
- `DETERMINISM.md` and `JOB_SYSTEM.md` — simulation determinism and threading.

## Runtime and content

- `CORE_SCRIPT.md` — CoreScript language/runtime.
- `LOGIC_LAYER_1_0.md` — gameplay logic contracts and maturity.
- `MOD_RUNTIME.md` — mod/content loading and deterministic load plans.
- `NOTIFICATION_RUNTIME.md` and `RESEARCH_ARCHITECTURE.md` — content runtimes.
- `SCRIPTED_GUI.md` — scripted GUI compiler and runtime boundary.
- `ECONOMY_ARCHITECTURE.md` — economy data and tick model.

## World and rendering

- `WORLD_COMPILER.md` — GIS/world-pack pipeline.
- `LIVING_MAP_ARCHITECTURE.md`, `POLITICAL_MAP_ARCHITECTURE.md`, and
  `MAP_MODE_ARCHITECTURE.md` — map data/runtime design.
- `TERRAIN_ARCHITECTURE.md`, `RENDERING_TARGET.md`, `GPU_TIERS.md`,
  `VULKAN_BACKEND.md`, and `VULKAN_PROBE.md` — rendering contracts.

## Status and research

- `VIC3_JOMINI_GAP_MATRIX.md` — clean-room observable capability comparison;
  it does not contain or reproduce proprietary source code.
- `RELEASE_STATUS_1_0_RC_GPU.md` — historical GPU-release checkpoint.
- `PERFORMANCE_BUDGET*.md` and `PERFORMANCE_DESIGN.md` — historical and current
  performance budgets. Files with numbered suffixes are milestone snapshots.

Documents describe different milestones. When a historical benchmark or status
conflicts with current verification, `FINAL_ENGINE_AUDIT.md` is authoritative.
