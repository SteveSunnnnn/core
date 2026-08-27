# Changelog

## Core 1.0 Development — Foreign Exchange (FX) & Multi-Currency International Trade — 2026-08-27
- Implemented `CurrencyStore` (`src/core/economy/CurrencyStore.cpp`) with support for registered currencies, floating/fixed/gold-standard exchange rate pegs, and overflow-safe fixed-point conversions.
- Unlocked multi-currency international trade in `EconomySystem::trade`, evaluating cross-currency price arbitrage against transport bands, clearing trade invoices at fair midpoints, and recording bilateral FX demand.
- Added country-level primary currency designations, foreign exchange reserves, and balance-of-payments tracking in `CountryStore`.
- Implemented deterministic weekly floating exchange rate formation driven by net international trade surplus and deficit pressures with bounded volatility clamps.
- Extended atomic save serialization with the `FX01` extension section in `SaveGame.cpp` while preserving backward compatibility for legacy and pre-FX saves.
- Added comprehensive unit and regression tests in `tests/economy_tests.cpp` covering currency conversion, cross-currency trade arbitrage, FX reserve accumulation, and exchange rate appreciation.

## Core 1.0 Development — Retained UI Runtime & Pin Updates — 2026-08-27
- Implemented `src/core/ui/ScriptedGuiRuntime.cpp` and wired it into `core_runtime`, completing the retained UI state machine.
- Added comprehensive unit tests for `ScriptedGuiRuntime` in `tests/scripted_gui_tests.cpp` covering data provider bindings, virtualized list/grid viewports, chart downsampling, and dirty diff generation.
- Added `ui_stable_node_key` helper to `src/core/ui/ScriptedGui.hpp` for deterministic node-hierarchy lookup.
- Clarified Vulkan-Headers 1.4.360 version pin and build-time dependency discovery in `THIRD_PARTY.md`.
- Updated engine audit and documentation reflecting the compiled and tested retained UI subsystem.

## Core 1.0 Development — Public Repository Packaging — 2026-08-27
- Declared the first-party source under the MIT License and added the canonical root `LICENSE` file.
- Added contribution and security policies, Git attributes, a documentation index, and Windows headless CI.
- Reworked the public README to match the verified Development Preview maturity, 26-test suite, build presets, and current directory layout.
- Corrected third-party notices for the vendored Khronos Vulkan-Headers revision and build-time SDL3/Zstandard/xxHash dependencies.
- Removed local machine paths and proprietary-product wording from public-facing example content and research metadata.
- Refreshed first-party source and SHA-256 manifests; generated packages and build trees remain excluded.

## Core 1.0 Development — Economy Closed-Loop & Render Fixes — 2026-08-27
- Added per-(market, good) inventory to MarketStore (serialized in save schema v4; v1/v3 remain read-only migrations with a byte-stable legacy checksum).
- Reworked price formation: demand clears against supply plus carried stock, unsold surplus becomes inventory (capped at 4 weeks of liquidity), and price pressure follows the net stock position with a symmetric clamp and dead-band correction.
- Added market-wide fulfillment ratios: production throughput is now rationed by worst input availability (derived stateless from serialized flows, so save/restore stays checksum-identical), and POP consumption payments plus standard-of-living targets scale with basket fulfillment.
- GDP accounting now uses wages + operating surplus instead of goods profit alone.
- Hoisted base prices out of the per-(market, good) price loop (removes the throwing `good()` accessor from the hot path).
- Render/UI: MSDF text shader now honors the atlas `px_range` push constant; political overlay alpha is no longer hardcoded to 45% (respects the pushed color); swapchain prefers MAILBOX over FIFO and accepts R8G8B8A8_SRGB surfaces; BindlessMaterialSystem recycles descriptor slots on unregister instead of leaking/aliasing them.

## Core 1.0 Development — Logic Foundation — 2026-08-26
- Kept the engine on the Core 1.0 release line as the initial foundation.
- Added CoreScript 1.0 scope traversal, saved scopes, ROOT/FROM/PREV/THIS and deterministic iterators.

- Added typed symbolic primitive arguments for stable country/content keys.
- Added data-driven Event/Decision/Journal/AI Action loading with event options and gameplay logging.
- Added persistent deterministic AI strategic plans (`ai_plan`) with priority, commitment, completion and action allow-lists.
- Added operational politics/diplomacy/warfare foundation systems and CoreScript bridges.
- Extended atomic runtime save/load to Gameplay, Journal/Event state, AI cooldowns and active plans using stable keys.
- Current writes use save schema v4; Core 1.0 schema-v1 and prior runtime schema-v3 are read-only migration formats.
- Added dedicated strategy-system and runtime-save regression tests; current Release CTest result is 9/9 PASS.
- This milestone does not claim Clausewitz/Jomini feature parity; see `docs/LOGIC_LAYER_1_0.md`.


## 1.0 RC-GPU — 2026-08-26

- Re-established a persistent 1.0 source tree and promoted strict Release warnings-as-errors as a release gate.
- Added GeographyStore and generic GrandStrategyStore records for technology/law/institution/company/trade/ownership/treaty/military/migration/politics/power-bloc/diplomatic-play/front/battle/colony/ship-design/investment primitives.
- Added Production Methods and expanded POP culture/religion/profession/literacy/qualification/wealth/political-strength/interest-group columns.
- Added complete current-world Save/Load, ReplayJournal and unified CoreEngine lifecycle with deterministic restore/continuation tests.
- Fixed stale MarketEntityIndex and ProvinceEntityIndex membership caches through explicit membership revisions.
- Hardened deterministic fixed-point arithmetic against extreme 64-bit overflow while retaining a normal-value fast path.
- Replaced transport bounding-box chunk enumeration with grid traversal proportional to traversed chunks.
- Reduced Living Map steady-state allocations/scans and added authored multi-chunk spatial placement.
- Added SpatialPlacementDatabase, settlement anchors and WorldBootstrap loading from `.coreworld`.
- Added production GIS compiler for province pages, coast SDF, terrain, masks, placement, anchors and adjacency.
- Hardened WorldPack and AssetPack malformed-input rejection.
- Added `.coreasset` asset packs, residency budgeting and `.corearch` architecture-kit binary cooker/reader.
- Added Strategy UI draw/batch/scissor/hit-region and virtualized-list foundation.
- Improved RenderGraph multi-reader/write/layout/queue hazard tracking and page streaming lifetime management.
- Added Vulkan 1.3 SDL3 desktop validation backend, validation/debug messenger, GPU capability report and Windows one-command validation gate.
- Added reference GLSL contracts for terrain, ocean, political overlay, Living Map and Strategy UI/MSDF text.
- Added Linux GitHub Actions CPU CI.

**Status:** RC-GPU. CPU/data/content tests are release candidates; Final requires target Windows physical-GPU validation and final visual pipeline integration/QA.

# Core Changelog

## 0.9 Living Map Data Foundation — 2026-08-25

- Added cold province-location SoA columns to POP and Building stores; economy hot loops remain unchanged.
- Added `ProvinceEntityIndex` CSR for province-owned POP/building aggregation.
- Added `LivingMapSystem` with per-province population/employment/SoL/building aggregation and quantized visual signatures.
- Added deterministic procedural visual generation with fixed 16-byte near-instance payloads and fixed 16-byte medium/far province clusters.
- Added 64 km Living Map chunks, per-chunk render versions and incremental rebuild/upload accounting.
- Added distance/budget driven `LivingMapStreamingPlanner`; near zoom uses full instances while medium/far zoom uses province clusters.
- Added `TransportNetwork`: province links are clipped into independently streamable 16-byte road/rail/canal segments; level changes update only affected segment/chunk versions.
- Added Living Map RenderGraph contract: transfer upload -> compute cull -> indirect transport/cluster/instance draws.
- Added deterministic cross-worker Living Map tests, incremental dirty tests, transport tests and `core_living_demo`.
- Added 8k-province / 300k-POP and 1M-POP Living Map benchmarks. Current-container steady-state baselines are ~0.6-0.8 ms and ~1.1-1.2 ms respectively; these are regression baselines, not hardware guarantees.
- Explicitly deferred final polygon-correct spawning, historical route polylines, architecture kits and live Vulkan rendering to the next visual/compiler phase.

## 0.8 Pops / Buildings / Markets Vertical Slice — 2026-08-25

- Added `EconomyDefinitions` with compact typed goods, building recipes and POP need profiles stored as flat flow arrays.
- Added `PopStore` SoA (32 bytes/cohort current hot layout), `BuildingStore`, flat market-good arrays and `MarketEntityIndex`.
- Added 64-bit deterministic fixed-point economy quantities/prices/money for the high-cardinality simulation path.
- Added weekly employment, production, industrial demand, POP consumption, market price convergence and settlement phases.
- Added bounded wage feedback, building cash/profit, POP income/standard-of-living, taxation and stable country GDP/population aggregation.
- Added market-owned parallel execution: market rows and entities are mutated without atomics; country results fold later in market ID order.
- Replaced per-POP checked getter/setter calls in hot kernels with validated SoA column spans after profiling identified access overhead.
- Replaced per-POP employer building scans with direct `BuildingId` remaining-capacity indexing.
- Added `(market, need_profile)` population aggregation and basket-cost caching so identical needs are evaluated once per profile/market instead of once per POP.
- Added deterministic serial-vs-parallel economy checksum tests, scarcity/price tests, storage-budget tests and `core_economy_demo`.
- Added 300k-POP and 1M-POP benchmarks. Current-container final baselines are ~3.5 ms average / ~3.1 ms median for 300k POPs, and ~18.5 ms average / ~15.7 ms median for 1M POPs with five execution slots. Measurements are regression baselines, not hardware guarantees.
- Revalidated the new economy path with Release `-Werror`, ASan/UBSan, ThreadSanitizer and JobSystem lifecycle stress.

## 0.7 Simulation Job System — 2026-08-25

- Added persistent `JobSystem` worker pool with caller-thread participation and a default cap of 15 background workers pending topology/NUMA probing.
- Added stable fixed-grain partitions; chunk identity is independent of worker count and dynamic claiming order.
- Added `DeterministicReduction` with chunk-index fold order and bit-stability tests across different worker counts.
- Added keyed deterministic RNG samples based on stable seed/stream/counter rather than worker or execution order.
- Added cache-line-separated `DeterministicCommandStage`; parallel chunks stage commands independently and flush in chunk order before global sequence assignment.
- Added worker-local linear scratch with per-job mark/rewind and nested-dispatch preservation.
- Added inline fast path for dispatches below eight jobs after profiling showed worker wake-up overhead dominated tiny work.
- Added conservative Tick DAG parallel execution: `Serial` remains default; only explicit `ParallelSafe` waves are eligible.
- Added `TickExecutionProfile`, `JobDispatchStats` and Graphviz DOT export for compiled tick graphs.
- Added nested dispatch fallback to inline execution and exception propagation from worker jobs.
- Stress-tested 10,000 repeated create/dispatch/destroy cycles.
- ThreadSanitizer found and drove fixes for a worker-shutdown lifetime race and a rare lost-wakeup bug in the initial condition-variable completion path; final completion uses C++20 `atomic::wait/notify`.
- Current-machine release baseline for a stable-chunk 5m-row synthetic dense kernel: ~4.2 ms serial vs ~1.6 ms parallel with five available worker slots; use only as a regression baseline.

## 0.6 CoreScript + mod runtime — 2026-08-25

- Added stable startup `SymbolTable`; compiled simulation code uses compact symbol/primitive IDs instead of hot-path text lookup.
- Reworked `ScriptRegistry` so triggers/effects receive 16-bit primitive IDs and native function-pointer dispatch.
- Added CoreScript lexer/parser with nested blocks, comments, quoted strings, numeric/date tokens and source diagnostics.
- Added typed `ScriptCompiler` and `ScriptProgramDatabase`.
- Added flat-AND trigger fast path after profiling the generic boolean VM regression.
- Added complex `all / any / not` RPN condition IR with fixed local evaluation stack and no runtime heap allocation.
- Added compiled scripted values for direct country SoA sources and compiled history effects.
- Added immutable `DefinitionDatabase` country content plus deterministic runtime instantiation.
- Added priority-based `VirtualFileSystem`: same logical path overlays and different-path duplicate definitions obey deterministic mod priority/load order.
- Added deterministic effective-content hash that includes semantic load priority, logical path and effective source bytes.
- Added symbol-backed localization tables with fallback lookup.
- Added `ContentLoader` and standalone `core_content_check` validator/compiler CLI.
- Added base content smoke fixtures for countries, scripts, history and English/Chinese localization.
- Added scope-error, boolean-condition, history, VFS override, content-hash and localization tests.
- Measured ~17-21 ms parse+compile for 10k simple scripts and restored ~51-53 ms for 5m two-trigger evaluations on the current machine after the fast-path optimization.

## 0.5 world compiler + resident map modes — 2026-08-24

- Added `.coreworld` seekable binary pack with 64-byte header, fixed sorted index and per-chunk keys.
- Added Zstd-per-chunk compression with raw fallback and 64-byte chunk alignment.
- Added self-described XXH3-64/FNV chunk integrity checks and O(1)-average duplicate-key detection.
- Added deterministic pack build hash, stable across manifest row order after chunk-key sorting.
- Added POSIX `pread()` random-access file path; runtime no longer reopens/seeks a stream per page.
- Added per-streaming-worker reusable compressed/decoded buffers and reusable Zstd decode context.
- Added `core_world_compiler` and `core_world_inspect` CLI tools.
- Added `PoliticalMapPageBundleView` for atomic 32 KiB province + 32 KiB coast payloads.
- Added compact `MapModeStore`: four scalar + three categorical modes stay resident in 112 KB at 8k provinces.
- Added optional GPU 16-bit storage capability; compatibility path may expand to 32-bit at upload.
- Added deterministic Britain technical world fixture and preview; coast is real GSHHS-derived data, provinces are explicitly synthetic/non-historical.
- Rejected 4 KiB-per-chunk alignment after measuring compressed-page hole overhead; default is now 64 bytes.
- Replaced scalar FNV hot-path chunk validation with XXH3, reducing the 10k cached page-read benchmark from ~864 ms to ~149 ms on this machine.
- Added world-pack/map-mode/bundle tests and clean Release + ASan/UBSan validation.

## 0.4 strategic map kernel — 2026-08-24

- Added 128x128 uint16 province-ID pages (32 KiB/page), with zero reserved for water.
- Added 128x128 int16 signed coast-distance pages (32 KiB/page, 0.5 m quantization).
- Added atomic 64 KiB province-ID + coast-distance streaming bundles.
- Added `PoliticalMapState`: province-owner/flags, RGBA8 country palette and per-province map-mode scalar buffers.
- Added sparse dirty spans plus dense-update fallback to full sequential buffer upload.
- Added political map RenderGraph upload and overlay passes.
- Added CPU `ProvincePickingCache` to avoid synchronous GPU readback for hover/selection.
- Added immutable CSR `ProvinceAdjacencyGraph` with land/river/strait/impassable flags and fixed-point costs.
- Added SDK-independent headless Vulkan loader probe; current container reaches Vulkan 1.4.309 loader and `vkCreateInstance`.
- Added political-map, picking, streaming, adjacency and Vulkan-probe tests/benchmarks.

## 0.3 terrain foundation — 2026-08-24

- Added double-precision Mercator projection and camera-relative GPU coordinate policy.
- Added logarithmic strategic camera with zoom-to-cursor approximation.
- Added constant-size 8-level terrain clipmap (400 patch instances by default).
- Added 65/33/17 grid LOD selection, reducing default terrain geometry from ~3.28M to ~0.844M triangles before culling.
- Added 65x65 uint16 absolute height pages: 8,450 bytes/page at 0.5 m precision.
- Added terrain page residency cache, streaming planner and adaptive upload byte budget.
- Added Terrain RenderGraph transfer -> compute cull -> indirect draw declaration.
- Added normalized GPU capability scoring/tiering for future Vulkan physical-device selection.

## 0.2 performance/render foundation — 2026-08-24

- Added frame-local linear allocator and 3-frame retirement ring.
- Added RenderGraph v0 with RAW/WAR/WAW dependency compilation, parallel batches and transition plan.
- Added non-blocking triple-buffer simulation/render snapshot exchange.
- Compacted country render records to 20 bytes and removed per-frame string copies.
- Removed world checksum from render hot path.
- Replaced deque command queue with retained-capacity contiguous vector queue.
- Replaced recursive modifier dirty propagation with reusable iterative traversal and linear batch recompute.
- Tick scheduler now precompiles dependency-free waves for later deterministic multithreading.
- Added microbenchmark target and explicit performance architecture rules.

## 0.1 foundation — 2026-08-24

Initial Core grand-strategy runtime foundation: deterministic time/commands, SoA world storage, tick
DAG, modifier graph, typed script primitive registry, render snapshots, tests and optional SDL3/Vulkan
desktop shell target.
