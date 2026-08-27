#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <vector>
#include <vulkan/vulkan.h>

#include "core/ui/StrategyUi.hpp"

struct SDL_Window;

namespace core {

class VulkanDesktopBackend {
public:
    VulkanDesktopBackend() = default;
    ~VulkanDesktopBackend();
    VulkanDesktopBackend(const VulkanDesktopBackend&) = delete;
    VulkanDesktopBackend& operator=(const VulkanDesktopBackend&) = delete;

    void initialize(SDL_Window* window, bool enable_validation);
    void draw_frame();
    // Stage a UI draw list for the next frame. Solid and polyline batches are
    // rendered through the UI pipeline with per-batch scissor; textured/MSDF
    // batches need bindless texture plumbing and are skipped for now.
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
    VkPipeline tonemap_pipeline_ = VK_NULL_HANDLE;
    VkBuffer living_vertices_ = VK_NULL_HANDLE;
    VkDeviceMemory living_vertices_memory_ = VK_NULL_HANDLE;
    VkBuffer living_instance_ = VK_NULL_HANDLE;
    VkDeviceMemory living_instance_memory_ = VK_NULL_HANDLE;
    VkBuffer ui_vertices_ = VK_NULL_HANDLE;
    VkDeviceMemory ui_vertices_memory_ = VK_NULL_HANDLE;

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
    struct UiGpuBatch {
        std::uint32_t first_index;
        std::uint32_t index_count;
        VkRect2D scissor;
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
