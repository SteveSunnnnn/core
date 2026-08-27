# Core Determinism Contract

Grand-strategy simulation must produce the same logical result regardless of worker scheduling and,
where supported by a kernel, regardless of worker count.

## Fixed partitioning

`StablePartition` uses an explicit grain size. Grain is a simulation/runtime constant, not derived
from CPU core count. A 4-core and 32-core machine therefore produce the same chunk boundaries.

This matters for:

- floating-point accumulation order;
- random stream assignment;
- command merge order;
- replay/OOS diagnostics.

Performance tuning may change grain between engine/content versions, but such a change is treated as
a deterministic-runtime change and must be versioned/tested.

## Deterministic reductions

`DeterministicReduction<T>` stores one partial per chunk ID. Work stealing/dynamic claiming can move a
chunk to another worker, but final folding always walks chunk IDs from 0..N-1. Tests compare the
bit representation of floating-point results across different worker counts.

## Randomness

Stateful RNG shared by workers is forbidden for parallel simulation. `DeterministicRng::keyed_u64`
and `keyed_unit` derive samples from stable `(base_seed, stream_key, counter)` values. A useful stream
key is an entity ID plus a system-specific salt; the counter can be the game tick or local sample
index.

A worker ID must never participate in a gameplay RNG seed.

## Command staging

Parallel jobs should not push directly into the global `CommandQueue`. `DeterministicCommandStage`
provides chunk-owned, cache-line-separated lanes. Each chunk may emit commands independently; flush
walks lanes in ascending chunk order and only then assigns global command sequence numbers.

This makes the commit order independent of which worker completed first.

## Mutable world access

`TickTaskMode::ParallelSafe` is an explicit contract. A parallel-safe task may only:

- write disjoint entity ranges;
- write its own chunk-local outputs;
- write deterministic staging/reduction slots;
- read immutable definitions or data guaranteed stable for that wave.

It may not mutate shared containers or overlapping SoA elements without a dedicated deterministic
staging protocol.

## Checksums and compatibility

Core already maintains world checksums, effective-content hashes and `.coreworld` build hashes. These
will be combined with replay command streams in later multiplayer/OOS work. Timing/profiler data is
never included in simulation checksums.
