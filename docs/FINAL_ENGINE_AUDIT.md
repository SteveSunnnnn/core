# Core 1.0 final engine audit

Audit date: 2026-08-28. Product version remains **1.0.0**.

## Verified in this workspace

- Debug headless: 26/26 tests pass.
- Release headless: 26/26 tests pass with test assertions enabled.
- Runtime smoke tests pass for the CLI, economy demo, grand-strategy demo, and Vulkan loader/device probe. The checked-in Britain technical pack is intentionally render-only (metadata/coast pages); world-definition bootstrap is covered by its dedicated in-memory regression fixtures rather than that pack.
- Release microbenchmarks complete successfully.
- Market settlement monetary state uses stable settlement-account IDs, is included in deterministic checksums, and round-trips through the optional `MON1` save section without raising the base save/product version.
- `MON1` validation rejects bad IDs, zero currency keys, wrong counts, duplicate sections, and corrupted payloads atomically. Pre-`MON1` v4 saves migrate to deterministic defaults.
- Global CoreScript variables, event targets, typed collections, call-frame
  bindings and deterministic random-draw counters round-trip through the tagged
  `GLB1` extension; invalid global scope references are rejected atomically.
- The normal world-aware weekly tick resolves due migration flows and routes
  finalized data-driven research diffusion through the persisted GameClock
  week, avoiding duplicate legacy diffusion when a research catalogue is active.
- The weekly economy regression now checks exact aggregate monetary conservation and uses enough partitions to exercise actual multi-worker execution.
- Modifier graphs evaluate deep dependency chains iteratively; a 50,000-node regression prevents stack-recursive evaluation from returning.
- Economy content ingestion rejects empty/duplicate definition keys, invalid `GoodId` references, non-positive flows, and offset/ID capacity overflow before mutating tables.
- `ScriptedGuiRuntime` retained UI subsystem evaluates typed bindings, virtualized collection lists/grids, chart downsampling, and generational dirty diffs with zero provider memory leaks.
- Monetary Standards & Currency Zones (`CurrencyStore`): Gold Standard (with gold shipping points), Silver Standard, Bimetallism (with Gresham's Law ratio tracking), and Fiat Floating are fully implemented. Hegemonic monetary sovereignty is dynamically assigned to the highest-prestige power in each currency union, collecting seigniorage and governing zone parity.
- Hume Gold Points Specie Arbitrage & Physical Shipping: When trade deficit causes FX market cost to exceed mint parity plus gold freight/insurance, bullion arbitrageurs automatically ship physical gold/silver, debiting central foreign reserves and defending parity. If reserves exhaust, specie convertibility is suspended and currency floats into fiat depreciation.
- Sovereign Debt, Bond Issuance & Credit Ratings: Nations can issue sovereign bonds against their borrowing capacity (scaled by Debt/GDP and credit rating AAA to D). Weekly debt interest is serviced from treasury; insolvency triggers sovereign default with credit freeze and prestige penalty.
- Domestic vs Foreign Exchange Settlement: Domestic transactions (wages, POP consumption, taxes, corporate retention) strictly settle in local primary currency, while cross-zone trade clears in foreign exchange with balance-of-payments tracking.
- Unified Real GDP Numeraire: Country GDP is normalized to global gold parity standard (`default_currency_key`) to eliminate inflation/currency devaluation distortions in international Great Power rankings, while domestic nominal GDP remains accessible for internal fiscal metrics.

## Release benchmark snapshot

Machine-specific numbers; use only as a regression baseline.

Latest Windows x64 Release run (2026-08-28, 16 workers): economy weekly tick
29.263 ms average / 30.708 ms p95 (30,000 buildings, 300,000 POPs), living-map
steady update 0.693 ms, and dense 5M-row kernel 5.446 ms serial / 1.444 ms
parallel. These values vary with host load; the historical measurements below
remain useful for trend comparison only.

- Economy: 128 markets, 32 goods, 30,000 buildings, 300,000 POPs; 16 job slots; weekly tick average 8.612 ms, p95 9.234 ms; checksum `0x3930b669bdc883ee`.
- Living map: 8,000 provinces, 300,000 POPs, 30,000 buildings; steady update average 0.303 ms; checksum `0x027e1768e9202909`.
- Dense 5M-row kernel: 2.763 ms serial, 1.075 ms parallel, identical reduction result.
- 50,000-node dirty modifier chain recompute: 0.389 ms.
- 10,000 CoreScript parse/compile batch: 28.726 ms; 5M two-trigger evaluations: 349.641 ms.

## Environment boundary

`release-desktop` configuration stops because this machine has a Vulkan runtime/driver but no Vulkan SDK headers/import library discoverable by CMake. The runtime probe succeeds with Vulkan 1.4.303 and two physical devices. Desktop source was therefore not compiled in this audit. CMake now checks Vulkan before downloading/configuring SDL so this prerequisite fails quickly.

## Remaining architectural risks

These are not test regressions introduced by the audit; they are boundaries of the current design that should remain visible:

- Commercial banking now uses balanced bank ledgers and persistent building-loan contracts with reserve/capital constraints, amortization, arrears, non-performing classification, default charge-offs, and insolvency. The market clearing account remains only the final fallback when regulated banks and funded domestic sectors cannot cover a building overdraft.
- Country treasury is still stored as binary64 for save compatibility and adapted to fixed-point milli-units at the economy boundary. A future internal finance schema should make the fixed-point treasury account authoritative.
- Multi-currency goods trade and FX clearing are active via `CurrencyStore`, with Gold/Silver/Bimetallism/Fiat monetary standards, currency unions, and prestige-based monetary sovereignty with seigniorage.
- Authored trade routes, import/export tariffs, and per-country international logistics budgets now constrain trade; foreign reserves and balance-of-payments entries use one global numeraire.
- Economy work is partitioned primarily by market. A very large single world market cannot use all cores without deterministic entity sub-partitioning.
- Economic definition keys are now unique and validated, but persistent saves still rely on dense definition IDs plus the content fingerprint rather than stable-key remapping for definition reorder migrations.
- `src/core/ui/ScriptedGuiRuntime.cpp` retained UI runtime is now fully implemented, wired into `core_runtime`, and verified in the automated test suite.
- Dense building/POP slot generations, liveness bitmaps and recycle order are
  included in the world checksum and `SLT1` save extension, preventing stale
  handles from silently becoming valid after long-running entity churn.

## Reproduction

```powershell
cmake --preset dev-headless
cmake --build --preset dev-headless -j 8
ctest --preset dev-headless --output-on-failure

cmake --preset release-headless
cmake --build --preset release-headless -j 8
ctest --preset release-headless --output-on-failure

.\build\release-headless\core_economy_bench.exe
.\build\release-headless\core_living_bench.exe
.\build\release-headless\core_bench.exe
```
