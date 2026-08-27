# Core 1.0 final engine audit

Audit date: 2026-08-27. Product version remains **1.0.0**.

## Verified in this workspace

- Debug headless: 26/26 tests pass.
- Release headless: 26/26 tests pass with test assertions enabled.
- Runtime smoke tests pass for the CLI, economy demo, grand-strategy demo, and Vulkan loader/device probe.
- Release microbenchmarks complete successfully.
- Market settlement monetary state uses stable settlement-account IDs, is included in deterministic checksums, and round-trips through the optional `MON1` save section without raising the base save/product version.
- `MON1` validation rejects bad IDs, zero currency keys, wrong counts, duplicate sections, and corrupted payloads atomically. Pre-`MON1` v4 saves migrate to deterministic defaults.
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

- Economy: 128 markets, 32 goods, 30,000 buildings, 300,000 POPs; 16 job slots; weekly tick average 8.612 ms, p95 9.234 ms; checksum `0x3930b669bdc883ee`.
- Living map: 8,000 provinces, 300,000 POPs, 30,000 buildings; steady update average 0.303 ms; checksum `0x027e1768e9202909`.
- Dense 5M-row kernel: 2.763 ms serial, 1.075 ms parallel, identical reduction result.
- 50,000-node dirty modifier chain recompute: 0.389 ms.
- 10,000 CoreScript parse/compile batch: 28.726 ms; 5M two-trigger evaluations: 349.641 ms.

## Environment boundary

`release-desktop` configuration stops because this machine has a Vulkan runtime/driver but no Vulkan SDK headers/import library discoverable by CMake. The runtime probe succeeds with Vulkan 1.4.303 and two physical devices. Desktop source was therefore not compiled in this audit. CMake now checks Vulkan before downloading/configuring SDL so this prerequisite fails quickly.

## Remaining architectural risks

These are not test regressions introduced by the audit; they are boundaries of the current design that should remain visible:

- Market clearing is a first closed-loop settlement layer, not yet a full commercial-bank/central-bank ledger. Unfunded building credit is represented by a clearing-account debit rather than a persistent loan contract with principal, interest, maturity, and default state.
- Country treasury is still stored as binary64 for save compatibility and adapted to fixed-point milli-units at the economy boundary. A future internal finance schema should make the fixed-point treasury account authoritative.
- Multi-currency goods trade and FX clearing are active via `CurrencyStore`, with Gold/Silver/Bimetallism/Fiat monetary standards, currency unions, and prestige-based monetary sovereignty with seigniorage.
- Economy work is partitioned primarily by market. A very large single world market cannot use all cores without deterministic entity sub-partitioning.
- Economic definition keys are now unique and validated, but persistent saves still rely on dense definition IDs plus the content fingerprint rather than stable-key remapping for definition reorder migrations.
- `src/core/ui/ScriptedGuiRuntime.cpp` retained UI runtime is now fully implemented, wired into `core_runtime`, and verified in the automated test suite.

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
