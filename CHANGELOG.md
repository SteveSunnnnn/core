# Changelog

## Core 1.0 Development — Unified Victorian grand-strategy UI theme — 2026-08-28

- Added the engine-wide design-token system `UiTheme`
  (`src/core/ui/UiTheme.hpp`): backgrounds, borders, accents, text,
  interaction states, shadow tiers, materials (wood / parchment / leather /
  brass / wax), typography scale, compact metrics and motion budgets. The
  default `UiTheme::victorian()` drives every drawing primitive; a draw list
  with no theme installed still renders the house style.
- Added smooth shading primitives: `quad_gradient` (per-vertex interpolated
  gradients, no banding) and `drop_shadow` (stacked penumbra layers); all
  materials and components were rebuilt on them, replacing hard-edged offset
  shadows and banded gradients.
- Expanded the generic component set: `ornate_header`, `window_frame`, `tab`,
  `dropdown_row`, `checkbox`, `radio`, `slider`, `scrollbar`, `input_box`,
  `list_row`, `table_header_cell`, `stat_row`, `notification_card` (five
  severity tiers), `modal_window`, `corner_ornaments`, `divider_ornament`,
  `separator` — all theme-driven with hover/pressed/selected/disabled/focus
  states and right-aligned numeric columns.
- Scripted GUI: panels now declare a surface role via `style =`
  (standard/wood/parchment/leather/recessed) and an optional `text` field
  rendered as an ornate header band; the painter maps roles onto theme
  materials. The bundled sample HUD (`content/base/ui/main.coregui`) was
  restructured into a wood top bar with recessed stat cells, wood navigation
  rail, parchment page windows with titled headers, and a parchment right
  context panel.
- Tooltip stack themed end to end (parchment body, leather title plaque,
  gold term links, separator rule, depth/lock indicators).
- Number presentation utilities: `ui_format_number` (thousands separators,
  decimals, compact K/M/B), `ui_format_delta`, `ui_delta_color`.
- Bundled OFL-licensed MSDF font atlases (`assets/fonts/`): Playfair Display
  Bold (`ui_display`) and EB Garamond Medium (`ui_body`); the desktop shell
  auto-loads `ui_body` when `CORE_UI_FONT_ATLAS` is unset. Attribution in
  `THIRD_PARTY.md`; bake recipe in `docs/UI_THEME.md`.
- Added `core_ui_theme_tests` (theme resolution, material tokens, component
  geometry, formatting, degenerate-input safety) and a scripted sample-HUD
  acceptance test that compiles and paints `main.coregui`.
- Documented the system in `docs/UI_THEME.md`.

## Core 1.0 Development — Engine/game boundary cleanup and stale-doc removal — 2026-08-28

- Completed the engine/game split: game composition code (`DesktopApp`,
  `GameAppController`, `StrategyHudSystem`, `GrandStrategyGui`,
  `GameProjectConfig`) lives in `src/game` as the `core_game_ui` target; the
  empty `src/core/platform/` directory was removed and configure-time guards
  keep `src/core` free of game-layer includes.
- Removed stale milestone documents: `PERFORMANCE_BUDGET_0_4/0_7/0_8/0_9.md`,
  `RELEASE_STATUS_1_0_RC_GPU.md`, `VIC3_JOMINI_GAP_MATRIX.md` and the frozen
  `VALIDATION_1_0.txt` snapshot; `docs/FINAL_ENGINE_AUDIT.md` remains the
  authoritative verification record.
- Kept `content/base` and `demo/` as the bundled sample game, loaded through
  the VFS/script pipeline and outside every engine target.
- Regenerated `SOURCE_FILES_1_0.txt` / `SOURCE_SHA256_1_0.txt` via
  `tools/update_source_manifest.py` (354 first-party files).

## Core 1.0 Development — Script-first bootstrap and DPI-safe UI text — 2026-08-28

- Fixed the Vulkan UI coordinate contract: logical UI, scissors and pointer
  input now share a top-left origin, removing the previous vertical mirror.
- Added logical-window to swapchain-pixel conversion for high-DPI scissors and
  viewport constants.
- Wired the desktop renderer to `.corefont` MSDF glyph metrics, real bearings
  and advances, derivative-based edge coverage and UTF-8 fallback glyphs. The
  fixed-cell/5x7 renderer remains diagnostics-only.
- Extended the font cooker to emit the runtime `COREIMG1` atlas alongside
  `.corefont` metrics, with optional KTX2 output.
- Added strict script definitions and transactional binding for goods,
  building types, production methods and need profiles.
- Added `GameContentRuntime`, a one-shot owner for symbols and compiled scripts
  that validates all content domains before installing a new game and binds
  the effective content hash into saves. The desktop shell uses it whenever
  `CORE_CONTENT_ROOT` is supplied.
- Added the script-first engine/content boundary document and new regression
  coverage for economy binding and full content bootstrap.
- Standardized topic-neutral engine naming: `StrategyHudSystem`,
  `MapDecorationRenderer`, `apply_warm_archival_grading` and
  `decorative_parchment_texture` replace setting-bound identifiers, with no
  legacy aliases. Built-in examples, tests and base-content headers now follow
  the same rule.
- Fixed the font cooker's default charset invocation for current
  `msdf-atlas-gen`, added a UTF-8/CJK glyph regression, documented that serif,
  sans-serif and CJK fonts share the same MSDF path, and made the pixel-font
  diagnostics fallback emit an explicit non-shipping warning.

## Core 1.0 Development — Deterministic Runtime Hardening & Global Script Persistence — 2026-08-28

- Added the tagged `GLB1` save extension for world-level CoreScript state:
  stable-key variables/parameters, event targets, typed collections, scope
  bindings and deterministic random-draw counters now survive save/load.
- Extended persistent-context validation to support an untyped global root while
  retaining world reference checks and atomic decode/commit behavior.
- Hardened stale entity handles across scripted traversal, bytecode evaluation,
  economy settlement, research, migration, warfare and construction queues by
  filtering destroyed SoA slots consistently.
- Wired data-driven weekly research diffusion to the persisted GameClock (with
  a legacy fallback only when no finalized research catalogue is present) and
  resolved due migration flows during the normal world-aware weekly tick.
- Added construction-queue invariants and overflow guards: project IDs remain
  monotonic, malformed targets are rejected before save/tick, completed
  expansions cannot wrap a building level, and weekly progress now participates
  in deterministic checksums with legacy CQ01 checksum migration retained.
- Added malformed UI geometry/tooltip overflow guards and data-backed desktop HUD
  values with explicit empty states instead of demo placeholders.
- Verified MSVC `/WX`, MinGW/GCC `-Werror`, Debug and Release headless builds;
  all 26 regression suites pass in each configuration.

## Core 1.0 Development — Realistic Governance, State Resistance & Natural Integration, Autonomous Trade & Treasury Funding — 2026-08-27

- **Treasury-Funded Institutions & Fiscal Budgeting** (`src/core/grand_strategy/GrandStrategyStore.cpp` / `hpp`):
  - Completely abolished abstract "administrative mana/points"; government institutions (1~5 levels across education, police, healthcare, bureaucracy) are directly funded with real money drawn weekly from the national Treasury (`GrandStrategyStore::run_institutions_weekly`).
  - Deficit spending or salary arrears reduce national prestige and degrade institutional effectiveness realistically.
- **Autonomous Market Trade Routes & Adaptive Merchant Scaling** (`src/core/economy/EconomySystem.cpp`):
  - Market routes are autonomously scaled by private commercial capital based on spatial price arbitrage spreads after freight logistics and bilateral import/export tariffs (+5%/week for profitable lanes; -10%/week shrinkage and eventual deregistration for unprofitable or unsupplied lanes).
- **State Resistance & Organic Core Integration** (`src/core/world/GeographyStore.cpp` / `hpp`):
  - Abolished artificial point-based state integration. Added continuous `state_resistance_ppm_` ($0 \sim 1,000,000$ ppm).
  - Newly conquered or annexed states start with high resistance, inducing tax evasion and reducing effective tax collection by up to 75%.
  - Resistance naturally decays toward zero ($-2,000$ ppm/week, $\approx 10.4\%$ annualized) when local population Standard of Living (SoL $\ge 9.0$) is high, cultural acceptance is maintained, and institutions are funded, organically transforming the territory into an integrated core without arbitrary point expenditure.
- **Continuous Social & Geopolitical Progression**:
  - POP qualification accumulation (`qualification_permyriad`) smoothly builds up weekly driven by local literacy rates and standard of living.
  - **Comprehensive Realistic Technology & Research Mechanics** (`src/core/research/ResearchSystem.cpp` / `hpp`):
    - **Multi-Pillar Innovation Generation**: Academic & popular innovation scales dynamically with literate population, amplified by up to $+75\%$ through government **Education Institutions (Tiers 1~5)**.
    - **Scientific Breakthroughs ("Eureka" Moments)**: Active research has a deterministic weekly chance ($2.5\%$) to trigger inspiration breakthroughs, delivering $+5\%$ immediate progress leaps.
    - **World-First Discovery Honors**: The pioneer country to first invent a technology in the world is awarded $+5.0$ international **Prestige**, reflecting global acclaim and technological leadership.
    - **Realistic Absorptive Capacity**: Foreign tech diffusion rate is directly modulated by the recipient nation's literacy and educational capacity, preventing backward, illiterate societies from instant assimilation while rewarding educational reforms.
    - **Strict Prerequisite & Era Gating**: Technologies cannot diffuse without all prerequisite dependencies unlocked and era thresholds achieved.
    - **Deterministic Probabilistic Rolls**: Diffusion requires peaceful diplomatic contact, trade agreements, and alliances, with deterministic `Fnv1a64` rolls.
  - Peacetime diplomatic relation drift, tension cooldown, and unengaged army readiness/organization recovery advance continuously.
- **Tagged `RES1` Save Extension** (`src/core/save/SaveGame.cpp`):
  - Emits and decodes state resistance state within a backward-compatible tagged section, preserving 100% downward compatibility across v1, v3, and v4 save schemas.
- **All 26/26 Unit & System Test Suites Passing 100%** across both `release-headless` and `dev-headless`.

## Core 1.0 Development — Gradual Production Method Transition, Construction Queue & 3D Monument Pipeline — 2026-08-27

- Implemented **Gradual Production Method (PM) Transition & Retooling** via a unified national **Construction Queue** (`ConstructionStore` in `src/core/economy/ConstructionStore.cpp` / `hpp`):
  - Changing a building's production method (PM) is no longer an instant switch; it enqueues a retooling project in the national construction queue.
  - Construction capacity points (scaled by base output, GDP, and private investment) and funds from the national `InvestmentPool` or state Treasury are invested each weekly tick.
  - Retooling progress (`pm_transition_progress_ppm`) smoothly advances from $0 \to 1,000,000$ ppm, preventing abrupt economic shocks, supply cliffs, and unemployment spikes.
  - Supports building expansion (`ExpandBuilding`), PM upgrade (`UpgradeProductionMethod`), and historical monument construction (`ConstructMonument`).
  - Supports dynamic queue management: priority reordering (`move_up`, `move_down`), pause/resume (`set_paused`), and project cancellation.
- Implemented **Procedural 3D Monument Generation Pipeline** (`tools/assets/generate_statue_of_liberty.py`):
  - Procedurally generates 4 Level-of-Detail (LOD0 to LOD3) models for the **Statue of Liberty (自由女神像)** featuring:
    - 11-pointed star fort base (Fort Wood) with stone ramparts.
    - Neoclassical rusticated granite pedestal with Greek key entablature, columned loggias, and observation balconies.
    - Classical draped Roman figure of Libertas with detailed cloth folds.
    - Radiant 7-spiked diadem crown (symbolizing the sun, 7 seas, and 7 continents).
    - Raised right arm gripping the Torch of Enlightenment with multi-faceted gilded flame geometry.
    - Left arm cradling the molded Tabula Ansata tablet inscribed with July 4, 1776.
  - Outputs binary glTF 2.0 (`.glb`), Wavefront (`.obj`), PBR material definition (`.coremat.json`), and LOD manifest (`.corelod.json`).
- Integrated **Construction Queue UI & Retained Widget Components**:
  - Registered `construction_queue` and `construction_project` contexts and properties in `ScriptedGuiSchema`.
  - Added `UiDrawList::construction_queue_row` in `StrategyUi.hpp` / `StrategyUi.cpp` rendering ornamental queue cards with wood/leather backgrounds, gilded progress bars, badges, ETA calculations, and pause status indicators.
- Added **`CQ01` Atomic Save Game Extension** (`src/core/save/SaveGame.cpp`):
  - Emits and decodes tagged construction queue sections with full backward compatibility and legacy checksum fallbacks.
- Added comprehensive unit tests in `tests/economy_tests.cpp` and `tests/scripted_gui_tests.cpp`, all verified across both `release-headless` and `dev-headless` with **26/26 CTest suites passing 100%**.

- Added SoA commercial-bank and persistent loan stores with stable keys, balanced assets/liabilities, reserve and capital requirements, interest, amortization, arrears, NPL classification, charge-offs, and insolvency.
- Added the optional `FIN1` save extension with atomic validation, deterministic checksums, and pre-FIN1 migration.
- Routed sovereign issuance through funded bank capacity or accumulated saver pools and returned debt service to recorded lenders.
- Added authored-route capacity, tariffs, international logistics budgets, global-numeraire BOP/reserve accounting, and cross-currency price ordering.
- Split monetary diagnostics into private-credit creation, central issuance, currency revaluation, and unexplained residual.
- Added CoreScript finance triggers/effects and logarithmic currency/bank lookup accelerators outside the high-cardinality hot path.

## Core 1.0 Development — Monetary Standards, Currency Zones & Monetary Sovereignty — 2026-08-27
- Refactored `CurrencyStore` (`src/core/economy/CurrencyStore.cpp`) to implement authentic **Monetary Standards**: `GoldStandard` (anchored to fine gold milligrams and bounded by gold transport points), `SilverStandard`, `Bimetallism` (governed by Gresham's Law ratio dynamics), and `FiatFloating` (market credit-driven).
- Implemented **Currency Zones (Monetary Unions)** and **Monetary Sovereignty**: The member country with highest prestige and power in a currency union dynamically holds monetary sovereignty, dictating metallic parity policy and harvesting seigniorage revenues into its national treasury.
- Added country prestige (`prestige`, `power_score`) to `CountryStore` and integrated it into authoritative state checksums.
- Enhanced `EconomySystem::trade` with multi-currency invoice clearing and Hume's Price-Specie Flow mechanism across international markets.
- Guaranteed rigorous domestic vs foreign exchange settlement separation: POP wages, consumption, building retention, and taxation strictly clear in domestic local currency, while cross-zone trade clears in foreign exchange.
- Standardized country GDP calculation to the global gold parity numeraire (`default_currency_key`), eliminating currency devaluation/inflation distortions in international Great Power rankings while retaining nominal local currency GDP for domestic fiscal accounting.
- Implemented Hume's classical **Gold & Silver Transport Points Specie Arbitrage**: When persistent trade deficits drive open-market FX costs above mint parity plus gold transport/insurance, bullion arbitrageurs automatically redeem paper for physical specie and ship gold/silver to clear the trade gap, directly debiting national central reserves. If reserves are drained to zero, convertibility is suspended and the currency is forced into fiat depreciation.
- Added **Sovereign Debt, Bond Issuance, Credit Ratings & Bond Markets** to `CountryStore`:
  - Countries can issue sovereign bonds to raise funds into their treasury up to credit-rating-scaled debt ceilings (`AAA` to `D`).
  - Implemented dynamic bond yields and credit rating evaluation based on Debt-to-GDP ratio, treasury coverage, and default history.
  - Weekly debt service interest is automatically deducted during economy settlement; insolvency triggers Sovereign Default (`CreditRating::D`), freezing bond markets and penalizing national prestige.
- Added rolling 52-week exchange rate history buffers in `CurrencyStore` enabling native in-game timeseries charts (`UiChartKind::Line`, `UiChartKind::Candlestick`) in `ScriptedGuiRuntime`.
- Updated `FX01` atomic save serialization to persist monetary standards, fine metal parities, zone leadership, country prestige, convertibility status, national debt, credit ratings, and bond yields.
- Added comprehensive unit and regression tests in `tests/economy_tests.cpp` covering metallic standard parities, Gresham's law ratio divergence, zone leadership election/transfer, specie drain/convertibility suspension, and sovereign bond issuance/default.

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
- This milestone does not claim complete production grand-strategy feature coverage; see `docs/LOGIC_LAYER_1_0.md`.


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
