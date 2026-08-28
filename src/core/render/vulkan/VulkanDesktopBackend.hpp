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

struct SDL_Window;

namespace core {

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
    void draw_frame();
    // Stage a UI draw list for the next frame. Solid and polyline batches are
    // rendered through the zero-descriptor UI pipeline; client-owned textured
    // batches use stable keys and are sampled when their optional resources
    // have been installed.
    void submit_ui(const UiDrawList& ui);
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
    void record_live_validation_draws(VkCommandBuffer command) const;
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
    VkExtent2D extent_{};
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
    VkPipelineLayout political_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout ui_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout tonemap_layout_ = VK_NULL_HANDLE;
    VkPipeline terrain_pipeline_ = VK_NULL_HANDLE;
    VkPipeline ocean_pipeline_ = VK_NULL_HANDLE;
    VkPipeline political_pipeline_ = VK_NULL_HANDLE;
    VkPipeline living_pipeline_ = VK_NULL_HANDLE;
    VkPipeline ui_pipeline_ = VK_NULL_HANDLE;
    VkPipeline ui_textured_pipeline_ = VK_NULL_HANDLE;
    VkPipeline ui_msdf_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout ui_textured_layout_ = VK_NULL_HANDLE;
    VkPipeline tonemap_pipeline_ = VK_NULL_HANDLE;
    VkBuffer living_vertices_ = VK_NULL_HANDLE;
    VkDeviceMemory living_vertices_memory_ = VK_NULL_HANDLE;
    VkBuffer living_instance_ = VK_NULL_HANDLE;
    VkDeviceMemory living_instance_memory_ = VK_NULL_HANDLE;
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
    VkSampler hdr_sampler_ = VK_NULL_HANDLE;
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
    UiFrameBuffer ui_frame_buffers_[frames_in_flight]{};

    std::atomic<std::uint32_t> validation_errors_{0};
    std::atomic<std::uint32_t> validation_warnings_{0};
    VkPhysicalDeviceProperties properties_{};
    std::uint64_t device_local_bytes_ = 0;
    std::uint32_t max_sampled_images_ = 0;
    bool supports_dynamic_rendering_ = false;
    bool supports_synchronization2_ = false;
    bool supports_timeline_ = false;
    bool supports_bda_ = false;
    bool supports_descriptor_indexing_ = false;
    bool supports_draw_indirect_count_ = false;
    bool supports_shader_draw_parameters_ = false;
};

} // namespace core
