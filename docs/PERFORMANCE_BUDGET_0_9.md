# Core 0.9 Living Map Performance Budget

These are architecture budgets and current-container regression baselines, not hardware guarantees.

## Runtime budgets

- Normal steady Living Map aggregation: target < 2 ms for 1M POP cohorts / ~80k buildings.
- A normal small visual change should upload only affected chunks, not the global Living Map buffer.
- Near-detail GPU instance payload: 16 bytes/instance.
- Medium/far province cluster payload: 16 bytes/province.
- Transport segment payload: 16 bytes/segment.
- Near instance planner has an explicit hard budget; far zoom must scale with province/chunk count.
- Living Map must not use synchronous GPU readback or block simulation on renderer residency.

## Current container baselines

### 8,000 provinces / 300,000 POP / 30,000 buildings

- First full visual build: ~4-6 ms in repeated local runs.
- Steady update with no visual signature changes: ~0.55-0.85 ms.
- Generated near instances: ~101k.
- First payload: ~1.67 MiB.
- 100 province-visible changes: ~0.65-1.0 ms, 44 dirty chunks, ~95 KiB upload.
- Camera streaming planner: ~0.03 ms per plan in the current synthetic world.

### 8,000 provinces / 1,000,000 POP / 80,000 buildings

- First full visual build: ~10-13 ms.
- Steady update: ~1.1-1.2 ms.
- Generated near instances: ~243k.
- First payload: ~3.84 MiB.
- 100 province-visible changes: ~1.2-1.5 ms and ~169 KiB upload.
- Living Map CPU cache memory: ~12.9 MiB in the current synthetic fixture.

### Transport technical network

For ~15,790 synthetic road/rail links across 8,000 provinces:

- Compile/split into chunk segments: ~2.8-3.0 ms.
- Runtime network cache: ~0.98 MiB.

## Optimization decisions retained

- Province spatial location is a separate cold SoA column.
- All high-cardinality GPU records are fixed 16-byte payloads.
- Far zoom uses one cluster per province rather than near-detail instances.
- Visual state is quantized before dirty detection to suppress meaningless weekly churn.
- Chunk versions drive incremental upload; render-cache epoch is excluded from content checksum.
- Camera planner walks candidate grid cells and binary-searches the sorted chunk directory rather
  than scanning all chunks.
- Transport geometry is split once and level updates mutate existing segment records in place.

## Profiling threshold for future allocator work

0.9 retains per-province `std::vector` placement caches because measured full rebuild and steady-state
costs are already below budget. Replace them with a paged/slab instance arena only if profiling on the
real Britain asset density shows allocator fragmentation or rebuild time above the budget. Premature
custom allocation is deliberately avoided.
