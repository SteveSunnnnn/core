# Core engine integration audit

Audit date: 2026-09-01. Product version remains **1.0.0**. This document records
the pure-engine build surface; authored game content and host applications are
external to this repository.

## Engine scope

`core_runtime` contains the reusable runtime from `src/core`. The repository's
remaining `src/apps` targets are engine utilities: the CLI, world compiler,
world inspector, and asset/architecture/material cookers. No game composition
layer, sample content package, demo application, or desktop shell is shipped.

## Current engine pass

- The headless targets `core_runtime`, `core_cli`, `core_world_compiler`,
  `core_world_inspect`, and the engine cookers configure and build.
- GLSL shader sources remain available for external host pipelines and can be
  compiled independently with `glslc`/`spirv-val`.
- First-party test, demo, benchmark, and desktop targets were removed in the
  engine-only cleanup.
- `.coreworld` packs and CoreScript content are external inputs; this repository
  does not ship an authored world or game package.

## Remaining architectural risks

These are boundaries of the current engine design that should remain visible:

- Commercial banking now uses balanced bank ledgers and persistent building-loan contracts with reserve/capital constraints, amortization, arrears, non-performing classification, default charge-offs, and insolvency. The market clearing account remains only the final fallback when regulated banks and funded domestic sectors cannot cover a building overdraft.
- Country treasury is still stored as binary64 for save compatibility and adapted to fixed-point milli-units at the economy boundary. A future internal finance schema should make the fixed-point treasury account authoritative.
- Multi-currency goods trade and FX clearing are active via `CurrencyStore`, with Gold/Silver/Bimetallism/Fiat monetary standards, currency unions, and prestige-based monetary sovereignty with seigniorage.
- Authored trade routes, import/export tariffs, and per-country international logistics budgets now constrain trade; foreign reserves and balance-of-payments entries use one global numeraire.
- Economy work is partitioned primarily by market. A very large single world market cannot use all cores without deterministic entity sub-partitioning.
- Economic definition keys are now unique and validated, but persistent saves still rely on dense definition IDs plus the content fingerprint rather than stable-key remapping for definition reorder migrations.
- Dense building/POP slot generations, liveness bitmaps and recycle order are
  included in the world checksum and `SLT1` save extension, preventing stale
  handles from silently becoming valid after long-running entity churn.

## Reproduction

```powershell
cmake --preset dev-headless
cmake --build --preset dev-headless -j 8

cmake --preset release-headless
cmake --build --preset release-headless -j 8
```
