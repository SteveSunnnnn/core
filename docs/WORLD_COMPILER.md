# Core World Compiler / `.coreworld` format — 0.5

## Goal

Runtime never parses GeoJSON/Shapefile or computes coast distance fields. Expensive GIS repair,
rasterization and derived-data generation happen offline. Runtime sees a seekable, indexed binary
pack whose cost scales with the pages currently needed by the camera.

## Pipeline

```text
GIS / DEM / historical source
        |
        v
source-specific preprocessors
        |
        +-- province-id 128x128 pages (R16_UINT convention, 0 = water)
        +-- coast signed-distance 128x128 pages (int16, 0.5 m units)
        +-- height pages
        +-- adjacency / definitions / strings
        |
        v
core_world_compiler
        |
        v
world.coreworld
```

Source-specific GIS tools may use Python/QGIS/GDAL/GeoPandas/Rasterio. Those dependencies are not
part of the shipping game runtime.

## Pack structure

- 64-byte little-endian header (format version 2).
- Independently compressed chunks.
- 64-byte data alignment by default.
- Sorted fixed-size index at end of file.
- Chunk identity: `(type, level, x, y, variant)`.
- Zstd level 3 when it saves at least 5%; otherwise raw.
- Checksum algorithm is stored per chunk (XXH3-64 when available; FNV-1a fallback), so packs are self-describing.
- Header stores a deterministic build hash derived from sorted chunk keys, raw sizes and chunk integrity hashes.
- Duplicate chunk keys are rejected by O(1)-average hash lookup during compilation.

A 64 KiB political page bundle stores:

```text
0 KiB  .. 32 KiB : 128x128 uint16 province ID page
32 KiB .. 64 KiB : 128x128 int16 coast signed-distance page
```

Keeping both layers atomic prevents a frame from observing different residency generations.

## Runtime I/O

`RandomAccessFile` uses POSIX `pread()` on Unix-like systems. This has no shared seek cursor, so
multiple streaming workers can read one file descriptor concurrently. The portable fallback uses a
mutex-protected stream.

Each streaming worker owns a `WorldPackDecodeScratch`:

- compressed input vector capacity is reused;
- decoded vector capacity is reused;
- Zstd decompression context is reused;
- no open/close per page;
- no temporary allocation per ordinary page after warmup.

The build hash is independent of manifest row order after key sorting; it is suitable for cache invalidation, save/mod compatibility diagnostics and OOS reports.

Index lookup is binary search over a contiguous sorted vector. A million lookups are cheap enough
that a hash table is not justified in the runtime hot path at current world sizes.

## Alignment decision

The initial prototype aligned every chunk to 4 KiB. That was rejected: highly compressible map
pages can shrink to hundreds or a few thousand bytes, making 4 KiB-per-chunk holes dominate pack
size. Core now defaults to 64-byte chunk alignment. OS page cache and storage sectors still operate
at their natural block size; logical chunks do not need 4 KiB offsets for ordinary buffered
`pread()`.

If DirectStorage/io_uring/O_DIRECT-style paths later prove useful, Core can introduce larger packed
I/O blocks without changing logical chunk keys.

## Britain technical fixture

`tools/build_britain_technical_demo.py` builds a deterministic 2048x2048 test fixture:

- land/coast: Basemap bundled GSHHS-derived land/sea mask;
- provinces: 72 deterministic synthetic Voronoi regions;
- page size: 128x128;
- 256 political/coast bundles;
- **not historical and not intended as 1836 content**.

It exists only to validate the complete data path before historical GIS is introduced.
