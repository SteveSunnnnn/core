# Contributing to Core

Core accepts focused changes that preserve its C++23, deterministic simulation,
SoA storage, and JobSystem architecture.

## Before opening a change

1. Build and run the Debug headless suite.
2. Build and run the Release headless suite.
3. Add a regression test for behavior or authoritative state changes.
4. Keep game-specific content in data files; do not hardcode one title's rules
   into the engine.
5. Do not submit proprietary source, extracted commercial-game assets, secrets,
   or content whose redistribution rights are unclear.

```powershell
cmake --preset dev-headless
cmake --build --preset dev-headless -j 8
ctest --preset dev-headless --output-on-failure

cmake --preset release-headless
cmake --build --preset release-headless -j 8
ctest --preset release-headless --output-on-failure
```

## Authoritative-state contract

New authoritative state must have:

- a stable key or stable runtime ID;
- save/load and migration behavior;
- deterministic checksum coverage;
- malformed-input validation;
- a dedicated regression test, including worker-count determinism where
  parallel execution is involved.

Keep generated build trees, validation output, local saves, and cooked demo
packages out of commits. See `.gitignore` for the current exclusions.

After adding, removing, or changing release files, refresh the first-party
integrity manifests with:

```powershell
python tools/update_source_manifest.py
```
