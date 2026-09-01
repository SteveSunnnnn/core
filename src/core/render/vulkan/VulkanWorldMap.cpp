#include "core/render/vulkan/VulkanDesktopBackend.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <string>

namespace core {
namespace {

void vkcheck(VkResult result, const char* what) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(what) + " failed: " +
                                 std::to_string(static_cast<int>(result)));
    }
}

} // namespace

void VulkanDesktopBackend::open_world_pack() {
    if (world_pack_path_.empty()) {
        throw std::runtime_error("world pack is required before Vulkan renderer initialization");
    }
    world_page_streamer_ = std::make_unique<WorldMapPageStreamer>(
        world_atlas_pages_per_side_, world_cpu_page_capacity_);
    std::string diagnostic;
    if (!world_page_streamer_->open(world_pack_path_, diagnostic))
        throw std::runtime_error(diagnostic.empty() ? "failed to open world map page source" : diagnostic);
    world_gpu_pages_.fill(std::nullopt);
    world_patch_staging_.clear();
    world_patch_staging_.reserve(world_atlas_pages_per_side_ * world_atlas_pages_per_side_);
    world_pending_patch_staging_.clear();
    world_pending_patch_staging_.reserve(world_atlas_pages_per_side_ * world_atlas_pages_per_side_);
    world_patch_count_ = 0u;
    world_stream_params_ = {{0.0f, 0.0f, 1.0f, 1.0f}};
    world_pending_stream_params_ = world_stream_params_;
    std::copy(std::begin(map_view_), std::end(map_view_), std::begin(world_render_view_));
    world_render_camera_[0] = map_camera_[0];
    world_render_camera_[1] = map_camera_[1];
    world_page_layout_initialized_ = false;
    world_height_layout_initialized_ = false;
    world_palette_layout_initialized_ = false;
    world_palette_dirty_ = true;
    world_palette_cpu_.assign(world_palette_bytes_, std::byte{0});

    // A deterministic neutral palette keeps the renderer useful before the
    // first simulation-side ownership upload. The actual desktop path calls
    // set_world_political_state() immediately after bootstrap.
    for (std::uint32_t id = 1u; id < 65'536u; ++id) {
        std::uint32_t hash = 2166136261u ^ id;
        hash ^= hash >> 13u;
        hash *= 0x9e3779b1u;
        const auto offset = static_cast<std::size_t>(id) * 4u;
        world_palette_cpu_[offset + 0u] = static_cast<std::byte>(96u + (hash & 0x5fu));
        world_palette_cpu_[offset + 1u] = static_cast<std::byte>(88u + ((hash >> 8u) & 0x5fu));
        world_palette_cpu_[offset + 2u] = static_cast<std::byte>(76u + ((hash >> 16u) & 0x5fu));
        world_palette_cpu_[offset + 3u] = static_cast<std::byte>(255u);
    }
    world_pack_ready_ = true;
}

void VulkanDesktopBackend::set_world_political_state(
    std::span<const ProvincePoliticalRecord> records,
    std::span<const Rgba8> country_colors) {
    if (world_palette_cpu_.size() != world_palette_bytes_) {
        world_palette_cpu_.assign(world_palette_bytes_, std::byte{0});
    }
    std::fill(world_palette_cpu_.begin(), world_palette_cpu_.end(), std::byte{0});
    const auto count = std::min<std::size_t>(records.size(), ProvinceRasterPage::max_province_count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto record = records[index];
        const auto offset = (index + 1u) * 4u;
        Rgba8 color{118u, 105u, 88u, 255u};
        if (record.owner_country < country_colors.size()) color = country_colors[record.owner_country];
        world_palette_cpu_[offset + 0u] = static_cast<std::byte>(color.r);
        world_palette_cpu_[offset + 1u] = static_cast<std::byte>(color.g);
        world_palette_cpu_[offset + 2u] = static_cast<std::byte>(color.b);
        world_palette_cpu_[offset + 3u] = static_cast<std::byte>(color.a);
    }
    world_palette_dirty_ = true;
}

void VulkanDesktopBackend::create_world_page_resources() {
    create_image_2d(world_atlas_size_, world_atlas_size_, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, 1u,
                    world_page_image_, world_page_memory_, world_page_view_);
    create_image_2d(world_atlas_size_, world_atlas_size_, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, 1u,
                    world_height_image_, world_height_memory_, world_height_view_);
    create_image_2d(256u, 256u, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, 1u,
                    world_palette_image_, world_palette_memory_, world_palette_view_);

    auto create_sampler = [&](VkFilter filter, VkSampler& sampler, const char* label) {
        VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        info.magFilter = filter;
        info.minFilter = filter;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.maxLod = 0.0f;
        vkcheck(vkCreateSampler(device_, &info, nullptr, &sampler), label);
    };
    create_sampler(VK_FILTER_NEAREST, world_page_sampler_, "vkCreateSampler(world page)");
    create_sampler(VK_FILTER_LINEAR, world_height_sampler_, "vkCreateSampler(world height)");
    create_sampler(VK_FILTER_NEAREST, world_palette_sampler_, "vkCreateSampler(world palette)");

    for (auto& frame : world_upload_frames_) {
        create_mapped_host_buffer(world_upload_buffer_bytes_, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  frame.buffer, frame.memory, frame.mapped);
    }
    for (auto& frame : world_patch_frames_) {
        create_mapped_host_buffer(world_patch_buffer_bytes_, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  frame.buffer, frame.memory, frame.mapped);
    }
}

void VulkanDesktopBackend::destroy_world_page_resources() noexcept {
    if (device_ != VK_NULL_HANDLE) {
        for (auto& frame : world_upload_frames_)
            destroy_mapped_host_buffer(frame.buffer, frame.memory, frame.mapped);
        for (auto& frame : world_patch_frames_)
            destroy_mapped_host_buffer(frame.buffer, frame.memory, frame.mapped);
        if (world_page_sampler_ != VK_NULL_HANDLE) vkDestroySampler(device_, world_page_sampler_, nullptr);
        if (world_height_sampler_ != VK_NULL_HANDLE) vkDestroySampler(device_, world_height_sampler_, nullptr);
        if (world_palette_sampler_ != VK_NULL_HANDLE) vkDestroySampler(device_, world_palette_sampler_, nullptr);
        world_page_sampler_ = VK_NULL_HANDLE;
        world_height_sampler_ = VK_NULL_HANDLE;
        world_palette_sampler_ = VK_NULL_HANDLE;
        destroy_image_2d(world_page_image_, world_page_memory_, world_page_view_);
        destroy_image_2d(world_height_image_, world_height_memory_, world_height_view_);
        destroy_image_2d(world_palette_image_, world_palette_memory_, world_palette_view_);
    }
    world_page_streamer_.reset();
    world_gpu_pages_.fill(std::nullopt);
    world_patch_staging_.clear();
    world_pending_patch_staging_.clear();
    world_patch_count_ = 0u;
    world_palette_cpu_.clear();
    world_pack_ready_ = false;
    world_palette_dirty_ = true;
    world_page_layout_initialized_ = false;
    world_height_layout_initialized_ = false;
    world_palette_layout_initialized_ = false;
}

void VulkanDesktopBackend::stream_world_pages() {
    if (!world_pack_ready_ || !world_page_streamer_ || !world_page_streamer_->ready()) return;
    const auto upload_budget = static_cast<std::uint64_t>(world_upload_page_budget_) *
                               WorldMapPageStreamingPlanner::upload_bytes;
    world_page_streamer_->plan(
        {{map_view_[0], map_view_[1], map_view_[2], map_view_[3]}},
        frames_presented_, upload_budget,
        std::span<const std::optional<WorldMapPageKey>>(world_gpu_pages_));
    world_pending_stream_params_ = world_page_streamer_->stream_params();
    const auto tiles = world_page_streamer_->visible_tiles();
    const auto patch_count = std::min<std::size_t>(tiles.size(),
                                                   world_atlas_pages_per_side_ *
                                                       world_atlas_pages_per_side_);
    world_pending_patch_staging_.clear();
    world_pending_patch_staging_.reserve(patch_count);
    for (std::size_t index = 0u; index < patch_count; ++index)
        world_pending_patch_staging_.push_back({tiles[index].map_rect});

    // A new camera window becomes active only when every page that will be
    // sampled by it is already in the atlas or is included in this frame's
    // bounded upload batch. Until then, retain the last complete map and its
    // matching view, so dragging cannot expose uninitialised/foreign pages.
    bool complete = !tiles.empty();
    for (std::size_t index = 0u; complete && index < patch_count; ++index) {
        const auto& tile = tiles[index];
        // Do not count a pending upload as resident yet. The page copy is
        // recorded later in this command buffer; activating the corresponding
        // map view now would leave the CPU-drawn boundary overlay one frame
        // ahead of the page atlas.
        if (world_gpu_pages_[tile.atlas_slot] != tile.key) complete = false;
    }
    if (complete) {
        world_stream_params_ = world_pending_stream_params_;
        world_patch_staging_ = std::move(world_pending_patch_staging_);
        world_patch_count_ = static_cast<std::uint32_t>(world_patch_staging_.size());
        std::copy(std::begin(map_view_), std::end(map_view_), std::begin(world_render_view_));
        world_render_camera_[0] = map_camera_[0];
        world_render_camera_[1] = map_camera_[1];
    }

    auto& patch_frame = world_patch_frames_[frame_index_];
    if (patch_frame.mapped != nullptr && world_patch_count_ != 0u) {
        std::memcpy(patch_frame.mapped, world_patch_staging_.data(),
                    static_cast<std::size_t>(world_patch_count_) * sizeof(WorldMapPatchGpu));
    }
}

void VulkanDesktopBackend::record_world_page_uploads(VkCommandBuffer command) {
    if (!world_pack_ready_ || !world_page_streamer_) return;
    const auto pending_uploads = world_page_streamer_->pending_uploads();
    if (pending_uploads.empty() && !world_palette_dirty_) return;
    auto& staging = world_upload_frames_[frame_index_];
    if (staging.buffer == VK_NULL_HANDLE || staging.mapped == nullptr) return;
    auto* destination = static_cast<std::byte*>(staging.mapped);
    const auto upload_count = std::min<std::size_t>(pending_uploads.size(), world_upload_page_budget_);
    const auto page_data_base = world_palette_bytes_;

    if (world_palette_dirty_) std::memcpy(destination, world_palette_cpu_.data(), world_palette_bytes_);
    for (std::size_t upload_index = 0; upload_index < upload_count; ++upload_index) {
        const auto& pending = pending_uploads[upload_index];
        const auto page_offset = page_data_base + upload_index * world_rgba_page_bytes_ * 2u;
        auto* page_destination = destination + page_offset;
        auto* height_destination = page_destination + world_rgba_page_bytes_;
        std::fill(page_destination, page_destination + world_rgba_page_bytes_, std::byte{0});
        std::fill(height_destination, height_destination + world_rgba_page_bytes_, std::byte{0});
        if (const auto* page = world_page_streamer_->page(pending.key); page != nullptr) {
            for (std::uint32_t y = 0u; y < world_atlas_page_size_; ++y) {
                for (std::uint32_t x = 0u; x < world_atlas_page_size_; ++x) {
                    const auto index = static_cast<std::size_t>(y) * world_atlas_page_size_ + x;
                    const auto id = page->province[index];
                    const auto coast = static_cast<std::uint16_t>(page->coast[index]);
                    page_destination[index * 4u + 0u] = static_cast<std::byte>(id & 0xffu);
                    page_destination[index * 4u + 1u] = static_cast<std::byte>(id >> 8u);
                    page_destination[index * 4u + 2u] = static_cast<std::byte>(coast & 0xffu);
                    page_destination[index * 4u + 3u] = static_cast<std::byte>(coast >> 8u);

                    const auto source_x = (x * 64u + 63u) / 127u;
                    const auto source_y = (y * 64u + 63u) / 127u;
                    const auto height = page->has_height
                        ? page->height[static_cast<std::size_t>(source_y) * 65u + source_x]
                        : static_cast<std::uint16_t>(24'000u);
                    height_destination[index * 4u + 0u] = static_cast<std::byte>(height & 0xffu);
                    height_destination[index * 4u + 1u] = static_cast<std::byte>(height >> 8u);
                    // The height page's B/A channels are intentionally unused
                    // by the scalar height payload. Carry the categorical lake and
                    // spatial masks there so the shader can classify water
                    // without another descriptor.
                    height_destination[index * 4u + 2u] =
                        static_cast<std::byte>(page->lake_mask[index] != 0u ? 255u : 0u);
                    height_destination[index * 4u + 3u] =
                        static_cast<std::byte>(page->spatial_mask[index] != 0u ? 255u : 0u);
                }
            }
        }
    }

    auto transition = [&](VkImage image, bool initialized, VkImageLayout old_layout,
                          VkImageLayout new_layout, VkPipelineStageFlags2 source_stage,
                          VkAccessFlags2 source_access, VkPipelineStageFlags2 destination_stage,
                          VkAccessFlags2 destination_access) {
        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.srcStageMask = initialized ? source_stage : VK_PIPELINE_STAGE_2_NONE;
        barrier.srcAccessMask = initialized ? source_access : 0;
        barrier.dstStageMask = destination_stage;
        barrier.dstAccessMask = destination_access;
        barrier.oldLayout = initialized ? old_layout : VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = new_layout;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(command, &dependency);
    };
    constexpr VkPipelineStageFlags2 transfer_stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    constexpr VkAccessFlags2 transfer_write = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    constexpr VkPipelineStageFlags2 fragment_stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    constexpr VkAccessFlags2 shader_read = VK_ACCESS_2_SHADER_READ_BIT;
    const bool has_page_upload = upload_count != 0u;
    if (world_palette_dirty_) transition(world_palette_image_, world_palette_layout_initialized_,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                          fragment_stage, shader_read, transfer_stage, transfer_write);
    if (has_page_upload) {
        transition(world_page_image_, world_page_layout_initialized_,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   fragment_stage, shader_read, transfer_stage, transfer_write);
        transition(world_height_image_, world_height_layout_initialized_,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   fragment_stage, shader_read, transfer_stage, transfer_write);
    }
    if (world_palette_dirty_) {
        VkBufferImageCopy copy{};
        copy.bufferOffset = 0u;
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1u;
        copy.imageExtent = {256u, 256u, 1u};
        vkCmdCopyBufferToImage(command, staging.buffer, world_palette_image_,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &copy);
    }
    for (std::size_t upload_index = 0; upload_index < upload_count; ++upload_index) {
        const auto& pending = pending_uploads[upload_index];
        const auto atlas_x = pending.atlas_slot % world_atlas_pages_per_side_;
        const auto atlas_y = pending.atlas_slot / world_atlas_pages_per_side_;
        VkBufferImageCopy page_copy{};
        page_copy.bufferOffset = page_data_base + upload_index * world_rgba_page_bytes_ * 2u;
        page_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        page_copy.imageSubresource.layerCount = 1u;
        page_copy.imageOffset = {static_cast<std::int32_t>(atlas_x * world_atlas_page_size_),
                                 static_cast<std::int32_t>(atlas_y * world_atlas_page_size_), 0};
        page_copy.imageExtent = {world_atlas_page_size_, world_atlas_page_size_, 1u};
        vkCmdCopyBufferToImage(command, staging.buffer, world_page_image_,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &page_copy);
        page_copy.bufferOffset += world_rgba_page_bytes_;
        vkCmdCopyBufferToImage(command, staging.buffer, world_height_image_,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &page_copy);
    }
    if (world_palette_dirty_) {
        transition(world_palette_image_, true, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, transfer_stage, transfer_write,
                   fragment_stage, shader_read);
        world_palette_layout_initialized_ = true;
        world_palette_dirty_ = false;
    }
    if (has_page_upload) {
        transition(world_page_image_, true, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, transfer_stage, transfer_write,
                   fragment_stage, shader_read);
        transition(world_height_image_, true, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, transfer_stage, transfer_write,
                   fragment_stage, shader_read);
        world_page_layout_initialized_ = true;
        world_height_layout_initialized_ = true;
        for (std::size_t index = 0; index < upload_count; ++index)
            world_gpu_pages_[pending_uploads[index].atlas_slot] = pending_uploads[index].key;
        world_page_streamer_->commit_uploaded(upload_count);
    }
}

} // namespace core
