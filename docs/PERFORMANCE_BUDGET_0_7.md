# Core 0.7 Simulation Performance Budget

These values are engineering targets and current-machine regression baselines, not promises for all
hardware.

## Architecture targets

- No per-entity heap allocation in dense POP/building/market kernels.
- No worker creation/destruction during ordinary ticks.
- No global queue lock per entity.
- Small job sets execute inline rather than waking the pool.
- Temporary worker allocations come from reusable linear scratch.
- Cross-worker floating reductions merge by deterministic chunk ID.
- Parallel commands stage locally and commit once.
- Renderer/GPU latency may not block the simulation worker pool.

## Current regression baseline

On the current execution environment, the release microbenchmark reports five worker slots including
the calling thread.

A synthetic 5,000,000-row arithmetic kernel using identical stable chunk boundaries measured about:

- serial chunked execution: ~4.2 ms;
- parallel execution: ~1.6 ms;
- same deterministic checksum;
- five worker slots available/used in the measured run.

1,000 deliberately tiny four-chunk dispatches execute via the inline fast path in roughly 0.08 ms
total in the measured run, demonstrating why these jobs should not wake the pool.

The exact numbers fluctuate with host load. `BENCHMARK.txt` is the canonical release-run snapshot.

## Next profiling targets

0.8 must measure real SoA kernels rather than synthetic arithmetic:

- 100k / 300k POP needs and income;
- employment matching;
- building production;
- market supply/demand aggregation;
- deterministic price solve;
- government budget aggregation;
- weekly tick end-to-end latency.

The first optimization priority will be memory bandwidth/data layout, then grain size, then scheduling.
