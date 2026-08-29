#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>
#include <vulkan/vulkan.h>

#include "core/ui/FontAtlas.hpp"
#include "core/ui/StrategyUi.hpp"
#include "core/render/flag/DynamicFlag3D.hpp"
#include "core/render/RenderQuality.hpp"

#include <chrono>

struct SDL_Window;

namespace core {

// Per-frame timing sample. GPU time comes from timestamp queries written
// around the whole command buffer; CPU time measures the host-side work in
// draw_frame() including command recording.
struct FrameTiming {
    double cpu_ms = 0.0;
    double gpu_ms = 0.0;
};

// Aggregated statistics over the frames since the last reset. Kept separate
// from the capability report so the benchmark harness can slice it freely.
struct RenderStats {
    std::uint64_t sampled_frames = 0;
    double avg_frame_ms = 0.0;
    double min_frame_ms = 0.0;
    double max_frame_ms = 0.0;
    double p95_frame_ms = 0.0;
    double avg_gpu_ms = 0.0;
    double avg_cpu_ms = 0.0;
    double fps = 0.0;
    std::uint64_t draw_calls_last_frame = 0;
};

// Expanded UI vertex used only at the renderer boundary.  UiDrawList keeps a
// compact AARRGGBB color; the Vulkan path uploads normalized float channels so
// the shader layout stays explicit and portable across backends.
struct UiGpuVertex {
    float x;
    float y;
    float u;
    float v;
    float r;
    float g;
    float b;
    float a;
};

class VulkanDesktopBackend {
public:
    VulkanDesktopBackend() = default;
    ~VulkanDesktopBackend();
    VulkanDesktopBackend(const VulkanDesktopBackend&) = delete;
    VulkanDesktopBackend& operator=(const VulkanDesktopBackend&) = delete;

    void initialize(SDL_Window* window, bool enable_validation);
    // Optional client-owned UI resources.  Paths are read before initialize()
    // and are never part of authoritative simulation state.
    void set_ui_font_atlas(std::filesystem::path image_path,
                           std::filesystem::path metrics_path = {}) {
        ui_font_atlas_path_ = std::move(image_path);
        ui_font_metrics_path_ = std::move(metrics_path);
    }
    void set_ui_world_map(std::filesystem::path path) { ui_world_map_path_ = std::move(path); }
    void set_dynamic_flag(DynamicFlag3D flag) { dynamic_flag_ = std::move(flag); }
    void draw_frame();
    // Stage a UI draw list for the next frame. Solid and polyline batches are
    // rendered through the zero-descriptor UI pipeline; client-owned textured
    // batches use stable keys and are sampled when their optional resources
    // have been installed.
    void submit_ui(const UiDrawList& ui);

    // Map viewport for the live validation renderer, in uv space:
    // (cx, cy) center and (hx, hy) half extents. Defaults to the full map.
    void set_map_view(float cx, float cy, float hx, float hy) noexcept {
        map_view_[0] = cx;
        map_view_[1] = cy;
        map_view_[2] = hx;
        map_view_[3] = hy;
    }
    void wait_idle();
    void write_report(const std::filesystem::path& path) const;

    [[nodiscard]] std::uint64_t frames_presented() const noexcept { return frames_presented_; }
    [[nodiscard]] std::uint32_t validation_errors() const noexcept {
        return validation_errors_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint32_t validation_warnings() const noexcept {
        return validation_warnings_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] bool validation_enabled() const noexcept { return validation_enabled_; }

    // Render quality control. The tier must be selected before initialize()
    // so the swapchain-dependent targets are created with the right format and
    // sample count. `Auto` resolves from device capabilities.
    void set_quality_tier(RenderQuality tier) noexcept { requested_quality_ = tier; }
    [[nodiscard]] RenderQuality quality_tier() const noexcept { return active_quality_; }
    [[nodiscard]] const RenderQualitySettings& quality_settings() const noexcept { return settings_; }

    // Internal render scale, adjustable at runtime. The 3D passes render into
    // a scene target of `extent_ * render_scale` and the tonemap pass upscales
    // into the swapchain, which makes it the escape hatch for weak hardware at
    // high display resolutions without a pipeline rebuild.
    //
    // The scale is only honoured when the tier uses an offscreen HDR target.
    // On the direct-to-swapchain legacy path the scene pass writes the
    // swapchain image itself, so its viewport must stay at full resolution.
    // set_render_scale() may be called before initialize() to choose the
    // startup scale, or at any time afterwards to resize the scene target.
    void set_render_scale(float scale);
    [[nodiscard]] float render_scale() const noexcept { return settings_.render_scale; }
    [[nodiscard]] VkExtent2D scene_extent() const noexcept { return scene_extent_; }

    // Timing. reset_stats() clears the accumulator; stats() reports over the
    // frames sampled since the last reset.
    void reset_stats() noexcept;
    [[nodiscard]] const RenderStats& stats() const noexcept { return stats_; }
    [[nodiscard]] std::uint32_t sample_count() const noexcept { return msaa_samples_; }

private:
    static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT types,
        const VkDebugUtilsMessengerCallbackDataEXT* data,
        void* user);

    void create_instance();
    void create_debug_messenger();
    void pick_device();
    void create_device();
    void create_swapchain();
    void destroy_swapchain();
    void create_sync();
    void destroy_sync();
    void recreate_swapchain();

    void create_live_validation_renderer();
    void destroy_live_validation_renderer();
    void create_ui_image(const std::filesystem::path& path,
                         VkImage& image,
                         VkDeviceMemory& memory,
                         VkImageView& view,
                         VkSampler& sampler,
                         std::uint32_t& width,
                         std::uint32_t& height);
    void destroy_ui_image(VkImage& image, VkDeviceMemory& memory,
                          VkImageView& view, VkSampler& sampler) noexcept;
    void load_font_slots(const std::filesystem::path& path);
    void load_ui_font_metrics();
    void create_hdr_targets();
    void destroy_hdr_targets();
    void create_depth_target();
    void destroy_depth_target();
    void create_scene_targets();
    void destroy_scene_targets();
    // Small helper for the offscreen targets. Dedicated allocations are fine
    // here: these images are long-lived and recreated only on resize.
    void create_image_2d(std::uint32_t width, std::uint32_t height, VkFormat format,
                         VkImageUsageFlags usage, VkImageAspectFlags aspect,
                         std::uint32_t samples, VkImage& image,
                         VkDeviceMemory& memory, VkImageView& view);
    void destroy_image_2d(VkImage& image, VkDeviceMemory& memory, VkImageView& view) noexcept;

    // Quality resolution: turns the requested tier into concrete settings
    // once device capabilities are known.
    void resolve_quality_tier();
    // Recomputes scene_extent_ from extent_ and the active render scale.
    void update_scene_extent() noexcept;
    VkFormat pick_depth_format() const;

    // Pipeline cache: SPIR-V compilation is the dominant cost when pipelines
    // are rebuilt on swapchain recreation, so the cache is persisted to disk.
    void create_pipeline_cache();
    void destroy_pipeline_cache() noexcept;

    // GPU timing.
    void create_query_pools();
    void destroy_query_pools() noexcept;
    void reset_query_pool(VkCommandBuffer command, std::uint32_t frame) const;
    void write_gpu_timestamp(VkCommandBuffer command, std::uint32_t frame, bool start) const;
    void collect_gpu_timing(std::uint32_t frame);
    void update_stats(double frame_ms, double gpu_ms, double cpu_ms);
    void record_live_validation_draws(VkCommandBuffer command) const;
    void record_ui_fallback(VkCommandBuffer command) const;
    void record_tonemap(VkCommandBuffer command) const;
    void record_fxaa(VkCommandBuffer command) const;
    void record_dynamic_flag_draw(VkCommandBuffer command, const UiModuleSlot& slot) const;
    void record_ui_draws(VkCommandBuffer command) const;
    void ensure_ui_frame_buffers();
    [[nodiscard]] VkShaderModule load_shader_module(const std::filesystem::path& path) const;
    [[nodiscard]] std::uint32_t find_memory_type(std::uint32_t bits, VkMemoryPropertyFlags required) const;
    void create_host_buffer(VkDeviceSize size,
                            VkBufferUsageFlags usage,
                            const void* data,
                            VkBuffer& buffer,
                            VkDeviceMemory& memory);

    SDL_Window* window_ = nullptr;
    bool validation_enabled_ = false;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkQueue present_queue_ = VK_NULL_HANDLE;
    std::uint32_t graphics_family_ = 0;
    std::uint32_t present_family_ = 0;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchain_format_ = VK_FORMAT_UNDEFINED;
    // True when the swapchain is an sRGB format, in which case the hardware
    // performs the transfer and the tonemap shader must not gamma-encode.
    bool srgb_swapchain_ = false;
    VkExtent2D extent_{};
    // Resolution of the offscreen 3D passes. Equals extent_ at render_scale
    // 1.0 and on the direct-to-swapchain legacy path.
    VkExtent2D scene_extent_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    std::vector<VkSemaphore> image_rendered_;

    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    static constexpr std::uint32_t frames_in_flight = 3;
    struct Frame {
        VkCommandBuffer command = VK_NULL_HANDLE;
        VkSemaphore acquired = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
    };
    Frame frames_[frames_in_flight]{};
    std::uint32_t frame_index_ = 0;
    std::uint64_t frames_presented_ = 0;

    std::filesystem::path shader_dir_;
    bool live_renderer_enabled_ = false;
    VkPipelineLayout fullscreen_layout_ = VK_NULL_HANDLE;
    float map_view_[4] = {0.5f, 0.5f, 0.5f, 0.5f};
    VkPipelineLayout political_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout flag_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout ui_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout tonemap_layout_ = VK_NULL_HANDLE;
    VkPipeline terrain_pipeline_ = VK_NULL_HANDLE;
    VkPipeline ocean_pipeline_ = VK_NULL_HANDLE;
    VkPipeline political_pipeline_ = VK_NULL_HANDLE;
    VkPipeline living_pipeline_ = VK_NULL_HANDLE;
    VkPipeline flag_pipeline_ = VK_NULL_HANDLE;
    VkPipeline ui_pipeline_ = VK_NULL_HANDLE;
    VkPipeline ui_textured_pipeline_ = VK_NULL_HANDLE;
    VkPipeline ui_msdf_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout ui_textured_layout_ = VK_NULL_HANDLE;
    VkPipeline tonemap_pipeline_ = VK_NULL_HANDLE;
    VkBuffer living_vertices_ = VK_NULL_HANDLE;
    VkDeviceMemory living_vertices_memory_ = VK_NULL_HANDLE;
    VkBuffer living_instance_ = VK_NULL_HANDLE;
    VkDeviceMemory living_instance_memory_ = VK_NULL_HANDLE;
    VkBuffer flag_vertices_ = VK_NULL_HANDLE;
    VkDeviceMemory flag_vertices_memory_ = VK_NULL_HANDLE;
    VkBuffer flag_indices_ = VK_NULL_HANDLE;
    VkDeviceMemory flag_indices_memory_ = VK_NULL_HANDLE;
    std::uint32_t flag_index_count_ = 0;
    std::optional<DynamicFlag3D> dynamic_flag_{};
    VkBuffer ui_vertices_ = VK_NULL_HANDLE;
    VkDeviceMemory ui_vertices_memory_ = VK_NULL_HANDLE;

    // Optional atlas-backed UI resources.  The map is an ordinary RGBA image;
    // fonts prefer Core's MSDF metrics and retain the legacy fixed-cell map as
    // a compatibility fallback for older client content.
    std::filesystem::path ui_font_atlas_path_;
    std::filesystem::path ui_font_metrics_path_;
    std::filesystem::path ui_world_map_path_;
    VkImage ui_font_image_ = VK_NULL_HANDLE;
    VkDeviceMemory ui_font_image_memory_ = VK_NULL_HANDLE;
    VkImageView ui_font_view_ = VK_NULL_HANDLE;
    VkSampler ui_font_sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout ui_font_descriptor_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool ui_font_descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet ui_font_descriptor_set_ = VK_NULL_HANDLE;
    VkImage ui_world_map_image_ = VK_NULL_HANDLE;
    VkDeviceMemory ui_world_map_memory_ = VK_NULL_HANDLE;
    VkImageView ui_world_map_view_ = VK_NULL_HANDLE;
    VkSampler ui_world_map_sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout ui_world_map_descriptor_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool ui_world_map_descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet ui_world_map_descriptor_set_ = VK_NULL_HANDLE;
    std::uint32_t ui_font_width_ = 0;
    std::uint32_t ui_font_height_ = 0;
    std::uint32_t ui_font_cell_ = 0;
    std::uint32_t ui_font_columns_ = 0;
    std::unordered_map<std::uint32_t, std::uint32_t> ui_font_slots_;
    std::optional<FontAtlas> ui_font_metrics_;
    float ui_logical_width_ = 1.0f;
    float ui_logical_height_ = 1.0f;
    float ui_scale_x_ = 1.0f;
    float ui_scale_y_ = 1.0f;

    // HDR scene target with MSAA and its resolved 1x copy consumed by the
    // tonemap pass. Recreated with the swapchain extent.
    VkFormat hdr_format_ = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkSampleCountFlagBits msaa_samples_ = VK_SAMPLE_COUNT_1_BIT;
    VkImage hdr_image_ = VK_NULL_HANDLE;
    VkDeviceMemory hdr_image_memory_ = VK_NULL_HANDLE;
    VkImageView hdr_view_ = VK_NULL_HANDLE;
    VkImage hdr_resolve_image_ = VK_NULL_HANDLE;
    VkDeviceMemory hdr_resolve_memory_ = VK_NULL_HANDLE;
    VkImageView hdr_resolve_view_ = VK_NULL_HANDLE;
    // Single-sample resolve target for the MSAA colour buffer. Distinct from
    // hdr_resolve_image_ above so the legacy members keep their meaning.
    VkImage hdr_msaa_resolve_image_ = VK_NULL_HANDLE;
    VkDeviceMemory hdr_msaa_resolve_memory_ = VK_NULL_HANDLE;
    VkImageView hdr_msaa_resolve_view_ = VK_NULL_HANDLE;
    // Intermediate target for the FXAA pass when MSAA is unavailable.
    VkImage fxaa_image_ = VK_NULL_HANDLE;
    VkDeviceMemory fxaa_memory_ = VK_NULL_HANDLE;
    VkImageView fxaa_view_ = VK_NULL_HANDLE;
    VkSampler fxaa_sampler_ = VK_NULL_HANDLE;
    VkDescriptorPool fxaa_descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet fxaa_descriptor_set_ = VK_NULL_HANDLE;
    VkSampler hdr_sampler_ = VK_NULL_HANDLE;
    bool hdr_targets_created_ = false;
    VkDescriptorPool hdr_descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout hdr_descriptor_layout_ = VK_NULL_HANDLE;
    VkDescriptorSet hdr_descriptor_set_ = VK_NULL_HANDLE;

    // UI draw-list staging: CPU copies filled by submit_ui, and one
    // persistently mapped vertex/index buffer pair per frame in flight.
    struct UiGpuBatch {
        std::uint32_t first_index;
        std::uint32_t index_count;
        VkRect2D scissor;
        UiBatchKind kind = UiBatchKind::Solid;
        std::uint64_t texture = 0;
        std::uint64_t order = 0;
    };
    struct UiFrameBuffer {
        VkBuffer vertex_buffer = VK_NULL_HANDLE;
        VkDeviceMemory vertex_memory = VK_NULL_HANDLE;
        void* vertex_mapped = nullptr;
        VkDeviceSize vertex_capacity = 0;
        VkBuffer index_buffer = VK_NULL_HANDLE;
        VkDeviceMemory index_memory = VK_NULL_HANDLE;
        void* index_mapped = nullptr;
        VkDeviceSize index_capacity = 0;
    };
    std::vector<UiGpuVertex> ui_staging_vertices_;
    std::vector<std::uint32_t> ui_staging_indices_;
    std::vector<UiGpuBatch> ui_staging_batches_;
    std::vector<UiModuleSlot> ui_staging_modules_;
    UiFrameBuffer ui_frame_buffers_[frames_in_flight]{};

    std::atomic<std::uint32_t> validation_errors_{0};
    std::atomic<std::uint32_t> validation_warnings_{0};
    VkPhysicalDeviceProperties properties_{};
    std::uint64_t device_local_bytes_ = 0;
    std::uint32_t max_sampled_images_ = 0;

    // ---- Render quality -------------------------------------------------
    // `requested_quality_` is what the caller asked for; `active_quality_` is
    // what was resolved against actual device capabilities once the physical
    // device was picked.
    RenderQuality requested_quality_ = RenderQuality::High;
    RenderQuality active_quality_ = RenderQuality::High;
    RenderQualitySettings settings_{};
    // Survives resolve_quality_tier() so a runtime scale change is not wiped
    // out the next time the tier is resolved.
    float requested_render_scale_ = 1.0f;
    bool discrete_gpu_ = false;

    // ---- Pipeline cache -------------------------------------------------
    VkPipelineCache pipeline_cache_ = VK_NULL_HANDLE;
    std::filesystem::path pipeline_cache_path_;

    // ---- Depth attachment ----------------------------------------------
    VkFormat depth_format_ = VK_FORMAT_UNDEFINED;
    VkImage depth_image_ = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;
    VkImageView depth_view_ = VK_NULL_HANDLE;

    // ---- FXAA ------------------------------------------------------------
    VkPipeline fxaa_pipeline_ = VK_NULL_HANDLE;

    // ---- GPU timing ------------------------------------------------------
    // One pool per frame in flight; each holds a start/end timestamp pair.
    VkQueryPool query_pools_[frames_in_flight]{};
    // Tracks which pools have had a timestamp pair written, so results are
    // never read back before the pool has been used. Written from a const
    // recording helper.
    mutable bool query_written_[frames_in_flight]{};
    bool timing_supported_ = false;
    double timestamp_period_ns_ = 1.0;
    double last_gpu_ms_ = 0.0;
    std::chrono::steady_clock::time_point frame_start_{};
    // Wall-clock interval between consecutive draw_frame() calls. This is what
    // determines the observed frame rate; the time spent inside draw_frame()
    // alone only measures host-side cost.
    std::chrono::steady_clock::time_point last_frame_time_{};
    bool have_last_frame_time_ = false;

    // ---- Statistics ------------------------------------------------------
    RenderStats stats_{};
    std::vector<double> frame_samples_;
    // Draw-call counter is incremented from const draw-recording helpers.
    mutable std::uint64_t draw_calls_ = 0;
    bool supports_dynamic_rendering_ = false;
    bool supports_synchronization2_ = false;
    bool supports_timeline_ = false;
    bool supports_bda_ = false;
    bool supports_descriptor_indexing_ = false;
    bool supports_draw_indirect_count_ = false;
    bool supports_shader_draw_parameters_ = false;
};

} // namespace core
