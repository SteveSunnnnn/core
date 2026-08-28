# Core 1.0 Development — Current Logic Foundation

## Status

This is an internal Core 1.0 development snapshot, not a 1.1 release and not a complete grand-strategy engine. The existing deterministic SoA, JobSystem, economy, world-pack and renderer boundaries are retained. This document records implemented logic contracts and explicitly separates them from the larger remaining feature surface.

## CoreScript foundation

- Typed runtime scopes: Country, State, Province, Pop and Market.
- Scope navigation and traversal through ROOT / FROM / PREV / THIS, saved scopes, parent resolution and `any_*` / `every_*` / deterministic `random_*` iterators.
- Scripted triggers/effects and non-Country scripted values.
- Typed/symbolic primitive arguments. Content may use stable symbols such as `form_alliance_with = FRA` and `start_law_enactment = universal_suffrage`; primitives that only accept numbers reject symbol arguments at compile time.
- Named typed signatures on scripts and scripted values, with required/optional parameters, literal defaults and concrete scope subtypes. A whole-database linker checks cross-file calls, unknown targets, missing/extra/duplicate arguments, static type/scope errors, undeclared parameter references and call cycles.
- Lexical variables, event targets and homogeneous typed collections are carried by `ScriptExecutionContext`. Scope collections retain their concrete element type.
- A live Event/Journal instance retains ROOT/FROM, variables, targets, collections and deterministic random-draw counters across ticks and save/load. Completed instances release this context.
- Random callsites derive from stable program identity and structural paths rather than source line or symbol insertion order; a saved per-callsite counter makes repeated draws deterministic but non-constant.
- Parser nesting/AST/numeric guards, stable-name collision diagnostics, call-depth checks and a transient VM work budget bound hostile or accidental content cost.
- Numeric same-scope scripts retain the compact fast path; advanced scope/symbol operations use the scoped interpreter path.

This remains a partial language runtime. Weighted/ordered iterators, general value bytecode, modifier integration, more domain scopes and a universal persistent script-state store are not complete. See `docs/CORE_SCRIPT.md`.

## Data-driven gameplay content

Content files can declare:

- `event` with options, potential/allow/effect, cooldown and gameplay log entries.
- `decision` with allow/effect/cooldown.
- `journal` with potential/completion/effect and persistent instance state.
- `ai_action` with validity, scripted utility, effect, base utility and cooldown.
- `ai_plan` with validity, scripted priority, completion, action allow-list, base priority and commitment duration.
- `notification` with localization/asset/category keys, priority, deduplication policy, lifetime, potential and scripted actions.

Gameplay definitions are resolved by stable content keys rather than vector positions. Event and Journal instances now also have monotonic 64-bit stable runtime identities. This does not yet provide delayed/queued events, a generic on-action scheduler, full Journal failure/progress semantics or complete Event presentation metadata.

## Notification runtime

Notifications are no longer represented by the gameplay log. The dedicated runtime supports Unread/Read/Actioned/Dismissed/Expired states, Stack/Suppress/Replace deduplication, occurrence counts, expiry, optional source and map-target scopes, and allow/effect actions. Definitions and action selections restore by stable keys even when content registration order changes.

Notification state is currently included in the engine checksum and the optional `NTF1` save section. Decode validates instance identity, definition/action keys, scopes, ticks, count/state invariants and the monotonic next-ID before the staged state is committed. Remaining work includes gameplay-signal routing, typed localization arguments, UI inbox/toast/map-focus integration and a deliberate authoritative-versus-presentation split. See `docs/NOTIFICATION_RUNTIME.md`.

## Mod and scripted-UI content foundations

- The `.coremod` manifest and load planner cover canonical stable IDs, strict SemVer ranges, required/optional dependencies, conflicts, load-before/after relationships, deterministic cycle diagnostics and a stable topological order. The frozen VFS mount plan and supplied package hashes feed the effective content hash. Semantic definition patch/extend/remove, compiled caches, signatures and hot reload remain outstanding.
- `ScriptedGuiCompiler` validates game-registered typed data contexts, property paths, commands, templates/screens and widget/list/grid/chart metadata into stable-key, compact-ID blueprints. Template cycles, type mismatches, unknown bindings and stable-key collisions are diagnosed at load time. The blueprint is immutable content, not authoritative state. A retained live runtime, reactive dirty propagation, focus/accessibility, validated dispatcher and renderer integration remain outstanding.

## AI strategic-plan runtime

The original deterministic utility selector remains available. The current foundation adds persistent strategic intent:

- One active plan per AI scope.
- Deterministic priority selection and key-based tie breaking.
- Plan commitment windows to prevent short-horizon oscillation.
- Plan invalidation/completion and higher-priority replacement after commitment.
- Utility selection restricted to the active plan's action set.
- Active-plan and action-cooldown state participate in deterministic runtime checksums and save/load.
- `CoreEngine` weekly Country AI uses planned execution; if no plans are defined it falls back to the original utility-action behavior.

## Politics, diplomacy and warfare reference implementation

The current implementation contains operational deterministic foundations rather than record-only skeletons:

- Government legitimacy/stability and law-enactment progress/support.
- Bilateral relations and treaty creation/breaking.
- Diplomatic Play phase progression and back-down handling.
- War/front/battle progression using army manpower/organization, casualties and war score.
- CoreScript triggers/effects bridge these systems to content.

These systems are not yet Victoria-class feature-complete; logistics, negotiations, subject systems, elections/movements and other deeper rules remain future work.

## Save/runtime compatibility

Current writes use internal save schema v4 while the product remains Core 1.0 development.

- Gameplay instances/logs, event option selections, AI cooldowns and AI active plans are serialized with stable definition/action/plan keys.
- The optional `GCT1` section serializes monotonic GameplayInstance IDs and the full context of every live Event/Journal chain. Legacy v4 payloads without this section receive deterministic migrated identities and reconstructed live contexts.
- The optional `NTF1` section serializes Notification instance IDs and rebinds definitions/actions by stable content key, not vector position.
- Decode is atomic: no authoritative world/runtime state is committed until payload integrity, references, scope ranges and checksums all validate.
- Core 1.0 schema-v1 saves are accepted through a read-only migration path using the original v1 world/grand-strategy checksum algorithm.
- Schema-v3 runtime saves remain a read-only migration format; new saves are written as v4.

## Verification coverage

The repository now has dedicated regression targets for CoreScript runtime/linking,
live gameplay-context save continuation, notifications, the mod planner/VFS hash
pipeline, and scripted GUI compilation in addition to the existing state, replay,
research and engine suites. These tests cover stable-key restoration under definition
registration reordering, atomic rejection when referenced content is missing, typed
collection invariants, random-salt stability, parser limits, notification expiry and
deduplication, mod registration permutation and GUI blueprint checksum stability.

Release acceptance still requires a clean C++23 build with
`CORE_WARNINGS_AS_ERRORS=ON`, all current CTest targets, `core_content_check`, shader
compilation/validation, and physical Windows/Vulkan validation where available. A
historical test-count snapshot is deliberately not treated as a permanent contract;
the current CTest manifest is authoritative.

## Remaining major logic gaps

Core 1.0 is still under development. High-priority remaining work includes:

- General expression bytecode, weighted/ordered iterators, modifier integration, more domain scopes and persistent script state outside Event/Journal instances.
- Generic on-action/delayed scheduler, complete Event/Decision/Journal semantics, notification signal/UI routing and presentation arguments.
- AI goal decomposition, diplomacy negotiation, construction/economic planning, military theatre/front assignment and long-horizon resource budgeting.
- Full political simulation: elections, governments/coalitions, movements, radicals/loyalists, revolutions, institutions and ideology interactions.
- Full diplomacy: obligations, subjects, treaty articles/negotiation, lobbies, power blocs, access/transit and peace settlement.
- Full warfare/navy: mobilization, formations, commanders, supply/logistics, occupation, pathing, naval missions/invasions and peace resolution.
- Trade/world-market, ownership/investment/company, migration/qualification and technology/law systems at production gameplay depth.
- Retained/reactive strategy UI runtime and renderer integration on top of the compiled scripted-GUI blueprint.
- Semantic mod patch/extend/remove, compiled caches/signing/hot reload, and complete save/mod compatibility/soak tests across all future authoritative stores.
