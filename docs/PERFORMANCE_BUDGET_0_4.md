# Core 0.4 Performance Budget Notes

## Decisions frozen this milestone

1. Province geometry is static and virtual-textured; ownership is indirection data.
2. Province hover is CPU-page sampling, never a synchronous GPU readback.
3. Dense dirty sets switch to full sequential buffer upload rather than expensive sparse sorting.
4. Province topology uses CSR; runtime simulation does not walk per-node heap containers.
5. Terrain, province-ID and coast pages share page keys so streaming/camera locality aligns.
6. Province-ID + coast pages upload atomically in 64 KiB bundles.
7. Simulation/render separation from 0.2 remains mandatory; map streaming may degrade visual detail,
   but must not stall simulation.

## Current memory arithmetic

- 400 visible province-ID pages: 12.5 MiB.
- 400 visible coast-distance pages: 12.5 MiB.
- both layers for 400 pages: 25 MiB uncompressed GPU payload.
- 512-page CPU picking mirror: ~16 MiB.
- 8,000 province political records: 62.5 KiB.
- 8,000 map-mode float values: 31.25 KiB.
- 256 RGBA8 country colors: 1 KiB.

The high-cardinality data is therefore page imagery, not political metadata. Optimization attention
should remain on page residency/compression and later material/vegetation streaming rather than on
micro-optimizing 8,000-entry owner tables.
