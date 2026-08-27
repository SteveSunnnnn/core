# Third-party notices

Core itself is distributed under the MIT License. Third-party components keep
their original licenses.

## Vendored

### Khronos Vulkan-Headers

`thirdparty/vulkan-headers/` contains a Vulkan-Headers distribution from The
Khronos Group. Its files are offered under Apache-2.0 or MIT terms as described
in `thirdparty/vulkan-headers/LICENSE.md`. The directory is marked as vendored
for GitHub language statistics.

- Upstream: `https://github.com/KhronosGroup/Vulkan-Headers.git`
- Imported version: Vulkan 1.4 Header Release (`VK_HEADER_VERSION 360` / `1.4.360`, compatible with Vulkan SDK 1.4.303+)
- Imported revision: `b51f6b865c18fc5b33990d12f75e8dfd672cede6`

## Acquired or detected at build time

- SDL 3.4.14 is fetched by CMake (`libsdl-org/SDL` tagged `release-3.4.14`) only when
  `CORE_BUILD_DESKTOP=ON`; SDL is distributed under the zlib License.
- Zstandard (`libzstd`) is an optional `.coreworld` compression backend detected via
  pkg-config (`CORE_HAS_ZSTD=1`); distributed under BSD/GPL dual licensing terms.
- xxHash (`libxxhash`) is an optional XXH3-64 integrity backend detected via
  pkg-config (`CORE_HAS_XXHASH=1`); distributed under BSD-2-Clause terms.
- Vulkan SDK/loader is required by the production desktop Vulkan target and is
  distributed separately by its respective vendor/Khronos packages.

## Technical demo data

Generated Britain technical-demo artifacts are excluded from normal commits by
`.gitignore`. Their land/coast source was based on the Basemap package's bundled
GSHHS-derived mask; synthetic province regions were produced by Core tooling.
Anyone publishing generated demo packages must verify and preserve the source
dataset's applicable notices and redistribution terms.

No proprietary commercial-game source code or extracted commercial-game asset
is licensed for inclusion in this repository.
