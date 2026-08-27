# Core 0.8 Economy Performance Budget

All figures below are regression measurements from the current execution environment, not universal
hardware guarantees.

## Reference workloads

### Medium vertical slice

- 128 markets
- 32 goods
- 30,000 buildings
- 300,000 POP cohorts
- 5 available execution slots in this container

Final observed baseline:

- weekly tick average: ~3.5 ms
- median: ~3.1 ms
- 95th percentile: ~6.6 ms
- economy mutable world memory: ~10.84 MB
- POP storage: 32 bytes/cohort
- market/entity index + economy scratch: ~1.45 MB

### Million-POP stress

- 256 markets
- 32 goods
- 80,000 buildings
- 1,000,000 POP cohorts
- 5 available execution slots

Final observed baseline:

- weekly tick average: ~18.5 ms
- median: ~15.7 ms
- 95th percentile: ~35.1 ms
- economy mutable world memory: ~35.24 MB
- POP storage: 32 bytes/cohort
- scratch/index: ~4.66 MB

Container scheduling noise produces visible long-tail outliers, so median and phase measurements should
be tracked alongside averages.

## Budgets going forward

Core should aim to keep the economic weekly tick below:

- 10 ms for ~300k POP cohorts on a recommended desktop CPU.
- 30 ms for ~1M POP cohorts on a recommended desktop CPU before trade/AI complexity is added.

The full game can therefore target several weekly ticks per second even after adding slower monthly AI,
trade, migration and political systems.

## Regression rules

1. Do not optimize tiny phases before profiling; production and price update are currently negligible.
2. A new feature that adds >20% to the same reference workload must identify the added work in the phase profiler.
3. No per-POP heap allocation during a tick.
4. No string/hash lookup in POP/building market loops.
5. No atomic floating-point accumulation for markets or countries.
6. Market layout rebuilds are not weekly hot-path work; rebuild only after batched structural changes.
7. Need baskets and recipes should remain compact CSR/flat-flow definitions.
8. Keep deterministic partition boundaries independent of worker count.
