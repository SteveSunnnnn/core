#include "core/render/vulkan/VulkanDesktopBackend.hpp"

#include <array>

namespace core {

void VulkanDesktopBackend::record_runtime_draws(VkCommandBuffer command) const {
    if (!runtime_renderer_enabled_) {
        return;
    }
    VkViewport viewport{};
    viewport.width = static_cast<float>(scene_extent_.width);
    viewport.height = static_cast<float>(scene_extent_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{};
    scissor.extent = scene_extent_;
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);

    // One push-constant block carries the map viewport and the stream-window
    // transform. Political borders come from exact categorical ID samples in
    // the resident page atlas rather than a softened full-world image.
    const std::array<float, 12> scene_push{{
        world_render_view_[0], world_render_view_[1],
        world_render_view_[2], world_render_view_[3],
        world_stream_params_[0], world_stream_params_[1],
        world_stream_params_[2], world_stream_params_[3],
        world_render_camera_[0], world_render_camera_[1],
        scene_extent_.height == 0u ? 1.0f :
            static_cast<float>(scene_extent_.width) / static_cast<float>(scene_extent_.height),
        0.0f}};

    const auto& world_patch_frame = world_patch_frames_[frame_index_];
    if (world_map_pipeline_ != VK_NULL_HANDLE && world_map_scene_descriptor_set_ != VK_NULL_HANDLE &&
        world_patch_frame.buffer != VK_NULL_HANDLE && world_patch_count_ != 0u) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, world_map_pipeline_);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, world_map_layout_,
                                0, 1, &world_map_scene_descriptor_set_, 0, nullptr);
        vkCmdPushConstants(command, world_map_layout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(scene_push), scene_push.data());
        const VkDeviceSize world_patch_offset = 0u;
        vkCmdBindVertexBuffers(command, 0, 1, &world_patch_frame.buffer, &world_patch_offset);
        // Six vertices form one page rectangle; the instance stream comes
        // from the CPU map streamer and is already clipped to the atlas window.
        constexpr std::uint32_t grid_resolution = 32u;
        vkCmdDraw(command, 6u * grid_resolution * grid_resolution,
                  world_patch_count_, 0, 0);
        ++draw_calls_;
    }

    // There is no implicit identity instance in the production map path. A
    // validation triangle used to be drawn when the living stream was empty,
    // which became a giant diagonal triangle over the world at far zoom.
    if (living_pipeline_ != VK_NULL_HANDLE && living_instance_count_ != 0u) {
        const auto& living_frame = living_frame_buffers_[frame_index_];
        if (living_frame.buffer != VK_NULL_HANDLE && living_frame.mapped != nullptr) {
            const VkBuffer living_buffers[]{living_vertices_, living_frame.buffer};
            const VkDeviceSize living_offsets[]{0, 0};
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, living_pipeline_);
            vkCmdBindVertexBuffers(command, 0, 2, living_buffers, living_offsets);
            vkCmdDraw(command, 3, living_instance_count_, 0, 0);
            ++draw_calls_;
        }
    }

    // Keep a small deterministic panel for the renderer validation probe when
    // no application UI was submitted.  Real frames use the indexed draw-list
    // path below, which is replaced every frame by submit_ui().
    //
    // The panel pipeline targets the swapchain, so on the HDR path it is
    // deferred to the UI pass rather than drawn into the scene target.
    if (ui_staging_indices_.empty() && !settings_.hdr_target) {
        record_ui_fallback(command);
    }
}



} // namespace core
