# Core Vulkan Backend — 0.2 implementation contract

The renderer backend is intentionally narrow and strategy-specific.

## Device tier

The first production PC tier targets modern discrete/integrated Vulkan hardware. Device selection
will score dedicated VRAM, queue topology and required feature support rather than simply choosing
the first discrete GPU.

Required baseline before creating the production renderer:

1. Dynamic rendering
2. Synchronization2
3. Timeline semaphores
4. Buffer device address
5. Shader draw parameters / indirect drawing support
6. Timestamp queries

Optional feature tiers can add descriptor indexing improvements, mesh shaders or other techniques,
but gameplay correctness cannot depend on them.

## Frame path

1. Poll SDL events.
2. Query completed GPU timeline value.
3. Acquire a retired `FrameRing` slot; only wait if all slots remain live.
4. Acquire swapchain image.
5. Consume newest simulation snapshot, if any.
6. Build/compile cached RenderGraph shape as required.
7. Record independent graph batches in parallel once the render worker pool lands.
8. Submit via `vkQueueSubmit2` / Synchronization2.
9. Signal next frame timeline value.
10. Present.

No `vkQueueWaitIdle` or `vkDeviceWaitIdle` is permitted in an ordinary frame.

## Swapchain resize

Resize/out-of-date recreation waits only for resources that must be destroyed, not the entire
engine. Camera/render extent updates are decoupled from simulation. Minimized windows may suspend
presentation while simulation continues.

## Uploads

Map chunks and assets use a persistently mapped staging ring. Upload requests are prioritized and
capped by a per-frame byte budget. Camera motion can therefore never trigger an unbounded upload
spike that causes visible hitches.

## Grand-strategy GPU scene

Long-term renderer data is structured around:

- province ID textures / lookup buffers,
- terrain chunk tables,
- instance buffers for settlements, vegetation, farms and industry,
- spline buffers for roads/rail/rivers,
- map-mode parameter buffers,
- indirect draw/dispatch command buffers.

The renderer does not maintain one C++ render object for each visible world object.


## 1.0 RC-GPU live validation draw

When `CORE_SHADER_DIR` points to the SPIR-V output produced by `tools/windows/validate_core.ps1`, the desktop backend creates graphics pipelines for Terrain, Ocean, Political Overlay, Living Map and solid Strategy UI. The 300-frame release gate executes all five pipelines under Dynamic Rendering and `VK_LAYER_KHRONOS_validation`. Render-finished binary semaphores are owned per swapchain image rather than per CPU frame, so they are not recycled before the presentation engine releases that image. This path is a production-API integration gate; final world-resource descriptor bindings, GPU culling/indirect dispatch and art-direction approval remain separate from the validation scene.
