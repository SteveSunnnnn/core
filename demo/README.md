# Core Britain technical fixture

- `britain_technical.coreworld` is a Core 0.5 world-pack pipeline fixture.
- `britain_technical_preview.png` visualizes the fixture before GPU rendering.
- Coast/land comes from the Basemap bundled GSHHS-derived mask.
- The 72 province regions are deterministic synthetic Voronoi partitions.
- This is **not** an 1836 historical map and must not be used as final game content.

Regenerate source pages with:

```bash
python tools/build_britain_technical_demo.py --out demo/britain_technical_input
./build/.../core_world_compiler demo/britain_technical_input/manifest.txt demo/britain_technical.coreworld
```
