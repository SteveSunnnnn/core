#include "core/render/vulkan/VulkanDesktopBackend.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace core {
namespace {

void vkcheck(VkResult result, const char* what) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(what) + " failed: " + std::to_string(static_cast<int>(result)));
    }
}

bool has_layer(const char* name) {
    std::uint32_t count = 0;
    vkcheck(vkEnumerateInstanceLayerProperties(&count, nullptr), "vkEnumerateInstanceLayerProperties(count)");
    std::vector<VkLayerProperties> properties(count);
    vkcheck(vkEnumerateInstanceLayerProperties(&count, properties.data()), "vkEnumerateInstanceLayerProperties(data)");
    return std::any_of(properties.begin(), properties.end(), [&](const auto& property) {
        return std::string_view{property.layerName} == name;
    });
}

bool has_device_extension(VkPhysicalDevice device, const char* name) {
    std::uint32_t count = 0;
    vkcheck(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr),
            "vkEnumerateDeviceExtensionProperties(count)");
    std::vector<VkExtensionProperties> properties(count);
    vkcheck(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, properties.data()),
            "vkEnumerateDeviceExtensionProperties(data)");
    return std::any_of(properties.begin(), properties.end(), [&](const auto& property) {
        return std::string_view{property.extensionName} == name;
    });
}

struct LivingVertex {
    float x;
    float y;
    float z;
};

[[nodiscard]] UiGpuVertex ui_gpu_vertex(const UiVertex& vertex) noexcept {
    // Core UI colors are stored as AARRGGBB (the high byte is alpha).  Keep
    // the public draw-list representation compact and expand only at the GPU
    // upload boundary, where the shader consumes four normalized floats.
    const auto rgba = vertex.rgba;
    constexpr float inv = 1.0f / 255.0f;
    return UiGpuVertex{
        vertex.x, vertex.y, vertex.u, vertex.v,
        static_cast<float>((rgba >> 16u) & 0xffu) * inv,
        static_cast<float>((rgba >> 8u) & 0xffu) * inv,
        static_cast<float>(rgba & 0xffu) * inv,
        static_cast<float>((rgba >> 24u) & 0xffu) * inv};
}

[[nodiscard]] std::array<std::uint8_t, 7> glyph_rows(char input) noexcept {
    const auto upper = (input >= 'a' && input <= 'z')
        ? static_cast<char>(input - ('a' - 'A')) : input;
    switch (upper) {
    case ' ': return {0,0,0,0,0,0,0};
    case 'A': return {0x0e,0x11,0x11,0x1f,0x11,0x11,0x11};
    case 'B': return {0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e};
    case 'C': return {0x0f,0x10,0x10,0x10,0x10,0x10,0x0f};
    case 'D': return {0x1e,0x11,0x11,0x11,0x11,0x11,0x1e};
    case 'E': return {0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f};
    case 'F': return {0x1f,0x10,0x10,0x1e,0x10,0x10,0x10};
    case 'G': return {0x0f,0x10,0x10,0x17,0x11,0x11,0x0f};
    case 'H': return {0x11,0x11,0x11,0x1f,0x11,0x11,0x11};
    case 'I': return {0x1f,0x04,0x04,0x04,0x04,0x04,0x1f};
    case 'J': return {0x01,0x01,0x01,0x01,0x11,0x11,0x0e};
    case 'K': return {0x11,0x12,0x14,0x18,0x14,0x12,0x11};
    case 'L': return {0x10,0x10,0x10,0x10,0x10,0x10,0x1f};
    case 'M': return {0x11,0x1b,0x15,0x15,0x11,0x11,0x11};
    case 'N': return {0x11,0x19,0x15,0x13,0x11,0x11,0x11};
    case 'O': return {0x0e,0x11,0x11,0x11,0x11,0x11,0x0e};
    case 'P': return {0x1e,0x11,0x11,0x1e,0x10,0x10,0x10};
    case 'Q': return {0x0e,0x11,0x11,0x11,0x15,0x12,0x0d};
    case 'R': return {0x1e,0x11,0x11,0x1e,0x14,0x12,0x11};
    case 'S': return {0x0f,0x10,0x10,0x0e,0x01,0x01,0x1e};
    case 'T': return {0x1f,0x04,0x04,0x04,0x04,0x04,0x04};
    case 'U': return {0x11,0x11,0x11,0x11,0x11,0x11,0x0e};
    case 'V': return {0x11,0x11,0x11,0x11,0x11,0x0a,0x04};
    case 'W': return {0x11,0x11,0x11,0x15,0x15,0x1b,0x11};
    case 'X': return {0x11,0x11,0x0a,0x04,0x0a,0x11,0x11};
    case 'Y': return {0x11,0x11,0x0a,0x04,0x04,0x04,0x04};
    case 'Z': return {0x1f,0x01,0x02,0x04,0x08,0x10,0x1f};
    case '0': return {0x0e,0x11,0x13,0x15,0x19,0x11,0x0e};
    case '1': return {0x04,0x0c,0x04,0x04,0x04,0x04,0x0e};
    case '2': return {0x0e,0x11,0x01,0x02,0x04,0x08,0x1f};
    case '3': return {0x1e,0x01,0x01,0x0e,0x01,0x01,0x1e};
    case '4': return {0x02,0x06,0x0a,0x12,0x1f,0x02,0x02};
    case '5': return {0x1f,0x10,0x10,0x1e,0x01,0x01,0x1e};
    case '6': return {0x0e,0x10,0x10,0x1e,0x11,0x11,0x0e};
    case '7': return {0x1f,0x01,0x02,0x04,0x08,0x08,0x08};
    case '8': return {0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e};
    case '9': return {0x0e,0x11,0x11,0x0f,0x01,0x01,0x0e};
    case '-': return {0x00,0x00,0x00,0x1f,0x00,0x00,0x00};
    case '#': return {0x0a,0x1f,0x0a,0x0a,0x1f,0x0a,0x00};
    case ':': return {0x00,0x04,0x04,0x00,0x04,0x04,0x00};
    case '.': return {0x00,0x00,0x00,0x00,0x00,0x0c,0x0c};
    case '/': return {0x01,0x02,0x02,0x04,0x08,0x08,0x10};
    case '%': return {0x19,0x19,0x02,0x04,0x08,0x13,0x13};
    case '+': return {0x00,0x04,0x04,0x1f,0x04,0x04,0x00};
    default: return {0x1f,0x11,0x15,0x11,0x15,0x11,0x1f};
    }
}

[[nodiscard]] VkRect2D ui_scissor(UiRect rect, VkExtent2D extent) noexcept {
    const auto full = VkRect2D{{0, 0}, extent};
    if (!std::isfinite(rect.x) || !std::isfinite(rect.y) ||
        !std::isfinite(rect.w) || !std::isfinite(rect.h) ||
        rect.w <= 0.0f || rect.h <= 0.0f) return full;
    const auto x0 = std::clamp(rect.x, 0.0f, static_cast<float>(extent.width));
    const auto y0 = std::clamp(rect.y, 0.0f, static_cast<float>(extent.height));
    const auto x1 = std::clamp(rect.x + rect.w, x0, static_cast<float>(extent.width));
    const auto y1 = std::clamp(rect.y + rect.h, y0, static_cast<float>(extent.height));
    return VkRect2D{
        {static_cast<std::int32_t>(x0), static_cast<std::int32_t>(y0)},
        {static_cast<std::uint32_t>(x1 - x0), static_cast<std::uint32_t>(y1 - y0)}};
}

VkPipelineColorBlendAttachmentState blend_attachment(bool alpha) {
    VkPipelineColorBlendAttachmentState state{};
    state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (alpha) {
        state.blendEnable = VK_TRUE;
        state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        state.colorBlendOp = VK_BLEND_OP_ADD;
        state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        state.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    return state;
}

} // namespace

VulkanDesktopBackend::~VulkanDesktopBackend() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        destroy_live_validation_renderer();
        destroy_sync();
        destroy_swapchain();
        vkDestroyDevice(device_, nullptr);
    }
    if (surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    }
    if (debug_ != VK_NULL_HANDLE) {
        const auto destroy_debug = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy_debug != nullptr) {
            destroy_debug(instance_, debug_, nullptr);
        }
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
    }
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDesktopBackend::debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* user) {
    auto* self = static_cast<VulkanDesktopBackend*>(user);
    const char* message = data != nullptr && data->pMessage != nullptr ? data->pMessage : "";
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0u) {
        self->validation_errors_.fetch_add(1u, std::memory_order_relaxed);
        std::cerr << "VULKAN_VALIDATION_ERROR: " << message << '\n';
    } else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0u) {
        self->validation_warnings_.fetch_add(1u, std::memory_order_relaxed);
        std::cerr << "VULKAN_VALIDATION_WARNING: " << message << '\n';
    }
    return VK_FALSE;
}

void VulkanDesktopBackend::initialize(SDL_Window* window, bool enable_validation) {
    window_ = window;
    validation_enabled_ = enable_validation;
    if (validation_enabled_ && !has_layer("VK_LAYER_KHRONOS_validation")) {
        throw std::runtime_error("VK_LAYER_KHRONOS_validation requested but unavailable");
    }
    if (const char* shader_dir = std::getenv("CORE_SHADER_DIR")) {
        shader_dir_ = std::filesystem::path{shader_dir};
    }
    create_instance();
    create_debug_messenger();
    if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_)) {
        throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
    }
    pick_device();
    create_device();
    create_swapchain();
    create_sync();
    if (!shader_dir_.empty()) {
        create_live_validation_renderer();
    }
}

void VulkanDesktopBackend::create_instance() {
    std::uint32_t count = 0;
    const char* const* required = SDL_Vulkan_GetInstanceExtensions(&count);
    if (required == nullptr || count == 0) {
        throw std::runtime_error("SDL returned no Vulkan instance extensions");
    }
    std::vector<const char*> extensions(required, required + count);
    if (validation_enabled_) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Core Engine";
    app.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
    app.pEngineName = "Core";
    app.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
    app.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo create_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create_info.pApplicationInfo = &app;
    create_info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();
    const char* layer = "VK_LAYER_KHRONOS_validation";
    if (validation_enabled_) {
        create_info.enabledLayerCount = 1;
        create_info.ppEnabledLayerNames = &layer;
    }
    vkcheck(vkCreateInstance(&create_info, nullptr, &instance_), "vkCreateInstance");
}

void VulkanDesktopBackend::create_debug_messenger() {
    if (!validation_enabled_) {
        return;
    }
    VkDebugUtilsMessengerCreateInfoEXT create_info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    create_info.pfnUserCallback = &VulkanDesktopBackend::debug_callback;
    create_info.pUserData = this;
    const auto create_debug = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
    if (create_debug == nullptr) {
        throw std::runtime_error("vkCreateDebugUtilsMessengerEXT unavailable");
    }
    vkcheck(create_debug(instance_, &create_info, nullptr, &debug_), "vkCreateDebugUtilsMessengerEXT");
}

void VulkanDesktopBackend::pick_device() {
    std::uint32_t count = 0;
    vkcheck(vkEnumeratePhysicalDevices(instance_, &count, nullptr), "vkEnumeratePhysicalDevices(count)");
    if (count == 0) {
        throw std::runtime_error("no Vulkan physical device");
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkcheck(vkEnumeratePhysicalDevices(instance_, &count, devices.data()), "vkEnumeratePhysicalDevices(data)");

    int best_score = -1;
    for (const auto device : devices) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        if (properties.apiVersion < VK_API_VERSION_1_3 ||
            !has_device_extension(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            continue;
        }

        std::uint32_t queue_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queue_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, queues.data());
        std::optional<std::uint32_t> graphics;
        std::optional<std::uint32_t> present;
        for (std::uint32_t index = 0; index < queue_count; ++index) {
            if ((queues[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u && !graphics.has_value()) {
                graphics = index;
            }
            VkBool32 supported = VK_FALSE;
            vkcheck(vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface_, &supported),
                    "vkGetPhysicalDeviceSurfaceSupportKHR");
            if (supported == VK_TRUE && !present.has_value()) {
                present = index;
            }
        }
        if (!graphics.has_value() || !present.has_value()) {
            continue;
        }

        VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDeviceVulkan11Features features11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
        features11.pNext = &features12;
        features12.pNext = &features13;
        VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features2.pNext = &features11;
        vkGetPhysicalDeviceFeatures2(device, &features2);
        if (features13.dynamicRendering != VK_TRUE ||
            features13.synchronization2 != VK_TRUE ||
            features12.timelineSemaphore != VK_TRUE ||
            features12.bufferDeviceAddress != VK_TRUE ||
            features11.shaderDrawParameters != VK_TRUE) {
            continue;
        }

        VkPhysicalDeviceMemoryProperties memory{};
        vkGetPhysicalDeviceMemoryProperties(device, &memory);
        std::uint64_t device_local = 0;
        for (std::uint32_t heap = 0; heap < memory.memoryHeapCount; ++heap) {
            if ((memory.memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0u) {
                device_local += memory.memoryHeaps[heap].size;
            }
        }

        int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 10000 : 0;
        score += static_cast<int>(properties.limits.maxImageDimension2D);
        if (features12.descriptorIndexing == VK_TRUE) {
            score += 1000;
        }
        if (features12.drawIndirectCount == VK_TRUE) {
            score += 750;
        }
        score += static_cast<int>(std::min<std::uint64_t>(device_local / (1024ull * 1024ull * 1024ull), 32ull)) * 50;
        if (score > best_score) {
            best_score = score;
            physical_ = device;
            properties_ = properties;
            graphics_family_ = *graphics;
            present_family_ = *present;
            supports_dynamic_rendering_ = true;
            supports_synchronization2_ = true;
            supports_timeline_ = true;
            supports_bda_ = true;
            supports_descriptor_indexing_ = features12.descriptorIndexing == VK_TRUE;
            supports_draw_indirect_count_ = features12.drawIndirectCount == VK_TRUE;
            supports_shader_draw_parameters_ = true;
            device_local_bytes_ = device_local;
            max_sampled_images_ = properties.limits.maxDescriptorSetSampledImages;
        }
    }
    if (physical_ == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "no Core-compatible Vulkan 1.3 device "
            "(dynamicRendering+synchronization2+timelineSemaphore+bufferDeviceAddress+shaderDrawParameters+swapchain)");
    }
}

void VulkanDesktopBackend::create_device() {
    const float priority = 1.0f;
    const std::set<std::uint32_t> families{graphics_family_, present_family_};
    std::vector<VkDeviceQueueCreateInfo> queue_infos;
    for (const auto family : families) {
        VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue.queueFamilyIndex = family;
        queue.queueCount = 1;
        queue.pQueuePriorities = &priority;
        queue_infos.push_back(queue);
    }

    VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    features12.pNext = &features13;
    features12.timelineSemaphore = supports_timeline_ ? VK_TRUE : VK_FALSE;
    features12.bufferDeviceAddress = supports_bda_ ? VK_TRUE : VK_FALSE;
    features12.descriptorIndexing = supports_descriptor_indexing_ ? VK_TRUE : VK_FALSE;
    features12.drawIndirectCount = supports_draw_indirect_count_ ? VK_TRUE : VK_FALSE;
    VkPhysicalDeviceVulkan11Features features11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    features11.pNext = &features12;
    features11.shaderDrawParameters = supports_shader_draw_parameters_ ? VK_TRUE : VK_FALSE;

    const char* extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo create_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    create_info.pNext = &features11;
    create_info.queueCreateInfoCount = static_cast<std::uint32_t>(queue_infos.size());
    create_info.pQueueCreateInfos = queue_infos.data();
    create_info.enabledExtensionCount = 1;
    create_info.ppEnabledExtensionNames = &extension;
    vkcheck(vkCreateDevice(physical_, &create_info, nullptr, &device_), "vkCreateDevice");
    vkGetDeviceQueue(device_, graphics_family_, 0, &graphics_queue_);
    vkGetDeviceQueue(device_, present_family_, 0, &present_queue_);
}

void VulkanDesktopBackend::create_swapchain() {
    VkSurfaceCapabilitiesKHR capabilities{};
    vkcheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &capabilities),
            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    std::uint32_t format_count = 0;
    vkcheck(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &format_count, nullptr),
            "vkGetPhysicalDeviceSurfaceFormatsKHR(count)");
    if (format_count == 0) {
        throw std::runtime_error("surface has no formats");
    }
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkcheck(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &format_count, formats.data()),
            "vkGetPhysicalDeviceSurfaceFormatsKHR(data)");
    auto format = formats.front();
    for (const auto& candidate : formats) {
        // Any SRGB swapchain format keeps shaders writing linear light correct;
        // accept both channel orders before falling back to a UNORM surface.
        if ((candidate.format == VK_FORMAT_B8G8R8A8_SRGB || candidate.format == VK_FORMAT_R8G8B8A8_SRGB) &&
            candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            format = candidate;
            break;
        }
    }
    swapchain_format_ = format.format;

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window_, &width, &height);
    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
        extent_ = capabilities.currentExtent;
    } else {
        extent_.width = std::clamp(static_cast<std::uint32_t>(std::max(width, 1)),
                                   capabilities.minImageExtent.width,
                                   capabilities.maxImageExtent.width);
        extent_.height = std::clamp(static_cast<std::uint32_t>(std::max(height, 1)),
                                    capabilities.minImageExtent.height,
                                    capabilities.maxImageExtent.height);
    }

    std::uint32_t image_count = std::max(capabilities.minImageCount + 1u, 3u);
    if (capabilities.maxImageCount > 0) {
        image_count = std::min(image_count, capabilities.maxImageCount);
    }

    VkSwapchainCreateInfoKHR create_info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    create_info.surface = surface_;
    create_info.minImageCount = image_count;
    create_info.imageFormat = format.format;
    create_info.imageColorSpace = format.colorSpace;
    create_info.imageExtent = extent_;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    const std::uint32_t queue_families[]{graphics_family_, present_family_};
    if (graphics_family_ != present_family_) {
        create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices = queue_families;
    } else {
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    create_info.preTransform = capabilities.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    // Prefer MAILBOX (low latency, no tearing) when offered; FIFO is the
    // guaranteed fallback.
    create_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    std::uint32_t present_mode_count = 0;
    vkcheck(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &present_mode_count, nullptr),
            "vkGetPhysicalDeviceSurfacePresentModesKHR(count)");
    std::vector<VkPresentModeKHR> present_modes(present_mode_count);
    vkcheck(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &present_mode_count, present_modes.data()),
            "vkGetPhysicalDeviceSurfacePresentModesKHR(data)");
    for (const auto candidate : present_modes) {
        if (candidate == VK_PRESENT_MODE_MAILBOX_KHR) {
            create_info.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
            break;
        }
    }
    create_info.clipped = VK_TRUE;
    vkcheck(vkCreateSwapchainKHR(device_, &create_info, nullptr, &swapchain_), "vkCreateSwapchainKHR");

    vkcheck(vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, nullptr), "vkGetSwapchainImagesKHR(count)");
    images_.resize(image_count);
    vkcheck(vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, images_.data()), "vkGetSwapchainImagesKHR(data)");
    views_.resize(images_.size());
    image_rendered_.resize(images_.size(), VK_NULL_HANDLE);
    for (std::size_t index = 0; index < images_.size(); ++index) {
        VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.image = images_[index];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = swapchain_format_;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.layerCount = 1;
        vkcheck(vkCreateImageView(device_, &view_info, nullptr, &views_[index]), "vkCreateImageView");

        VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkcheck(vkCreateSemaphore(device_, &semaphore_info, nullptr, &image_rendered_[index]),
                "vkCreateSemaphore(image_rendered)");
    }
}

void VulkanDesktopBackend::destroy_swapchain() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    for (const auto semaphore : image_rendered_) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, semaphore, nullptr);
        }
    }
    image_rendered_.clear();
    for (const auto view : views_) {
        vkDestroyImageView(device_, view, nullptr);
    }
    views_.clear();
    images_.clear();
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    }
    swapchain_ = VK_NULL_HANDLE;
}

void VulkanDesktopBackend::create_sync() {
    VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.queueFamilyIndex = graphics_family_;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkcheck(vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_), "vkCreateCommandPool");

    std::array<VkCommandBuffer, frames_in_flight> commands{};
    VkCommandBufferAllocateInfo allocate_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate_info.commandPool = command_pool_;
    allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate_info.commandBufferCount = frames_in_flight;
    vkcheck(vkAllocateCommandBuffers(device_, &allocate_info, commands.data()), "vkAllocateCommandBuffers");

    for (std::uint32_t index = 0; index < frames_in_flight; ++index) {
        frames_[index].command = commands[index];
        VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkcheck(vkCreateSemaphore(device_, &semaphore_info, nullptr, &frames_[index].acquired),
                "vkCreateSemaphore(acquired)");
        VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkcheck(vkCreateFence(device_, &fence_info, nullptr, &frames_[index].fence), "vkCreateFence");
    }
}

void VulkanDesktopBackend::destroy_sync() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    for (auto& frame : frames_) {
        if (frame.fence != VK_NULL_HANDLE) {
            vkDestroyFence(device_, frame.fence, nullptr);
            frame.fence = VK_NULL_HANDLE;
        }
        if (frame.acquired != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, frame.acquired, nullptr);
            frame.acquired = VK_NULL_HANDLE;
        }
        frame.command = VK_NULL_HANDLE;
    }
    if (command_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, command_pool_, nullptr);
        command_pool_ = VK_NULL_HANDLE;
    }
}

std::uint32_t VulkanDesktopBackend::find_memory_type(std::uint32_t bits, VkMemoryPropertyFlags required) const {
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physical_, &memory);
    for (std::uint32_t index = 0; index < memory.memoryTypeCount; ++index) {
        const bool supported = (bits & (1u << index)) != 0u;
        const bool has_flags = (memory.memoryTypes[index].propertyFlags & required) == required;
        if (supported && has_flags) {
            return index;
        }
    }
    throw std::runtime_error("no compatible Vulkan memory type");
}

void VulkanDesktopBackend::create_host_buffer(VkDeviceSize size,
                                               VkBufferUsageFlags usage,
                                               const void* data,
                                               VkBuffer& buffer,
                                               VkDeviceMemory& memory) {
    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkcheck(vkCreateBuffer(device_, &buffer_info, nullptr, &buffer), "vkCreateBuffer");

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, buffer, &requirements);
    VkMemoryAllocateInfo allocate_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate_info.allocationSize = requirements.size;
    allocate_info.memoryTypeIndex = find_memory_type(
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkcheck(vkAllocateMemory(device_, &allocate_info, nullptr, &memory), "vkAllocateMemory");
    vkcheck(vkBindBufferMemory(device_, buffer, memory, 0), "vkBindBufferMemory");

    void* mapped = nullptr;
    vkcheck(vkMapMemory(device_, memory, 0, size, 0, &mapped), "vkMapMemory");
    std::memcpy(mapped, data, static_cast<std::size_t>(size));
    vkUnmapMemory(device_, memory);
}

VkShaderModule VulkanDesktopBackend::load_shader_module(const std::filesystem::path& path) const {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("cannot open SPIR-V shader: " + path.string());
    }
    const std::streamoff end = stream.tellg();
    if (end <= 0 || (static_cast<std::uint64_t>(end) % sizeof(std::uint32_t)) != 0u) {
        throw std::runtime_error("invalid SPIR-V byte size: " + path.string());
    }
    const auto byte_size = static_cast<std::size_t>(end);
    std::vector<std::uint32_t> words(byte_size / sizeof(std::uint32_t));
    stream.seekg(0, std::ios::beg);
    stream.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(byte_size));
    if (!stream) {
        throw std::runtime_error("failed reading SPIR-V shader: " + path.string());
    }
    VkShaderModuleCreateInfo create_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    create_info.codeSize = byte_size;
    create_info.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    vkcheck(vkCreateShaderModule(device_, &create_info, nullptr, &module), "vkCreateShaderModule");
    return module;
}

void VulkanDesktopBackend::create_live_validation_renderer() {
    if (!std::filesystem::is_directory(shader_dir_)) {
        throw std::runtime_error("CORE_SHADER_DIR is not a directory: " + shader_dir_.string());
    }

    VkPipelineLayoutCreateInfo empty_layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    vkcheck(vkCreatePipelineLayout(device_, &empty_layout, nullptr, &fullscreen_layout_),
            "vkCreatePipelineLayout(fullscreen)");

    VkPushConstantRange political_push{};
    political_push.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    political_push.offset = 0;
    political_push.size = sizeof(float) * 4u;
    VkPipelineLayoutCreateInfo political_layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    political_layout_info.pushConstantRangeCount = 1;
    political_layout_info.pPushConstantRanges = &political_push;
    vkcheck(vkCreatePipelineLayout(device_, &political_layout_info, nullptr, &political_layout_),
            "vkCreatePipelineLayout(political)");

    VkPushConstantRange ui_push{};
    ui_push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    ui_push.offset = 0;
    ui_push.size = sizeof(float) * 2u;
    VkPipelineLayoutCreateInfo ui_layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    ui_layout_info.pushConstantRangeCount = 1;
    ui_layout_info.pPushConstantRanges = &ui_push;
    vkcheck(vkCreatePipelineLayout(device_, &ui_layout_info, nullptr, &ui_layout_),
            "vkCreatePipelineLayout(ui)");

    // 3D passes render into an MSAA RGBA16F target; the tonemap pass samples
    // the resolved copy and writes the (possibly non-sRGB) swapchain.
    const auto color_samples = properties_.limits.framebufferColorSampleCounts;
    msaa_samples_ = VK_SAMPLE_COUNT_1_BIT;
    for (const auto candidate : {VK_SAMPLE_COUNT_8_BIT, VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_2_BIT}) {
        if ((color_samples & candidate) != 0u) {
            msaa_samples_ = candidate;
            break;
        }
    }

    VkDescriptorSetLayoutBinding sampled_binding{};
    sampled_binding.binding = 0;
    sampled_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampled_binding.descriptorCount = 1;
    sampled_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo descriptor_layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptor_layout_info.bindingCount = 1;
    descriptor_layout_info.pBindings = &sampled_binding;
    vkcheck(vkCreateDescriptorSetLayout(device_, &descriptor_layout_info, nullptr, &hdr_descriptor_layout_),
            "vkCreateDescriptorSetLayout(hdr)");

    VkPushConstantRange tonemap_push{};
    tonemap_push.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    tonemap_push.offset = 0;
    tonemap_push.size = sizeof(std::int32_t);
    VkPipelineLayoutCreateInfo tonemap_layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    tonemap_layout_info.setLayoutCount = 1;
    tonemap_layout_info.pSetLayouts = &hdr_descriptor_layout_;
    tonemap_layout_info.pushConstantRangeCount = 1;
    tonemap_layout_info.pPushConstantRanges = &tonemap_push;
    vkcheck(vkCreatePipelineLayout(device_, &tonemap_layout_info, nullptr, &tonemap_layout_),
            "vkCreatePipelineLayout(tonemap)");

    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 1;
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    vkcheck(vkCreateDescriptorPool(device_, &pool_info, nullptr, &hdr_descriptor_pool_),
            "vkCreateDescriptorPool(hdr)");
    VkDescriptorSetAllocateInfo set_allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    set_allocate.descriptorPool = hdr_descriptor_pool_;
    set_allocate.descriptorSetCount = 1;
    set_allocate.pSetLayouts = &hdr_descriptor_layout_;
    vkcheck(vkAllocateDescriptorSets(device_, &set_allocate, &hdr_descriptor_set_),
            "vkAllocateDescriptorSets(hdr)");

    VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.maxLod = VK_LOD_CLAMP_NONE;
    vkcheck(vkCreateSampler(device_, &sampler_info, nullptr, &hdr_sampler_), "vkCreateSampler(hdr)");

    const auto fullscreen = load_shader_module(shader_dir_ / "fullscreen.vert.spv");
    const auto terrain = load_shader_module(shader_dir_ / "terrain.frag.spv");
    const auto ocean = load_shader_module(shader_dir_ / "ocean.frag.spv");
    const auto political = load_shader_module(shader_dir_ / "political_overlay.frag.spv");
    const auto living_vertex = load_shader_module(shader_dir_ / "living.vert.spv");
    const auto living_fragment = load_shader_module(shader_dir_ / "living.frag.spv");
    const auto ui_vertex = load_shader_module(shader_dir_ / "ui.vert.spv");
    const auto ui_fragment = load_shader_module(shader_dir_ / "ui.frag.spv");

    auto create_pipeline = [&](VkShaderModule vertex,
                               VkShaderModule fragment,
                               VkPipelineLayout layout,
                               bool alpha,
                               const VkPipelineVertexInputStateCreateInfo* vertex_input,
                               VkFormat color_format,
                               VkSampleCountFlagBits samples) {
        const VkPipelineShaderStageCreateInfo stages[]{
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", nullptr}};
        VkPipelineVertexInputStateCreateInfo empty_vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo input_assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport_state{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = samples;
        const auto blend = blend_attachment(alpha);
        VkPipelineColorBlendStateCreateInfo blend_state{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend_state.attachmentCount = 1;
        blend_state.pAttachments = &blend;
        const VkDynamicState dynamic_states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamic_states;
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachmentFormats = &color_format;
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.pNext = &rendering;
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = vertex_input != nullptr ? vertex_input : &empty_vertex;
        info.pInputAssemblyState = &input_assembly;
        info.pViewportState = &viewport_state;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pColorBlendState = &blend_state;
        info.pDynamicState = &dynamic;
        info.layout = layout;
        VkPipeline pipeline = VK_NULL_HANDLE;
        vkcheck(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
                "vkCreateGraphicsPipelines");
        return pipeline;
    };

    // The current frame graph renders directly into the swapchain.  Keep the
    // pipeline attachment format identical to that target; the HDR resources
    // are optional forward-looking infrastructure and are not part of this
    // validation renderer yet.
    terrain_pipeline_ = create_pipeline(fullscreen, terrain, fullscreen_layout_, false, nullptr, swapchain_format_, VK_SAMPLE_COUNT_1_BIT);
    ocean_pipeline_ = create_pipeline(fullscreen, ocean, fullscreen_layout_, true, nullptr, swapchain_format_, VK_SAMPLE_COUNT_1_BIT);
    political_pipeline_ = create_pipeline(fullscreen, political, political_layout_, true, nullptr, swapchain_format_, VK_SAMPLE_COUNT_1_BIT);

    const VkVertexInputBindingDescription living_bindings[]{
        {0, sizeof(LivingVertex), VK_VERTEX_INPUT_RATE_VERTEX},
        {1, sizeof(float) * 16u, VK_VERTEX_INPUT_RATE_INSTANCE}};
    const VkVertexInputAttributeDescription living_attributes[]{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
        {1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
        {2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 4u},
        {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 8u},
        {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 12u}};
    VkPipelineVertexInputStateCreateInfo living_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    living_input.vertexBindingDescriptionCount = 2;
    living_input.pVertexBindingDescriptions = living_bindings;
    living_input.vertexAttributeDescriptionCount = 5;
    living_input.pVertexAttributeDescriptions = living_attributes;
    living_pipeline_ = create_pipeline(living_vertex, living_fragment, fullscreen_layout_, true, &living_input, swapchain_format_, VK_SAMPLE_COUNT_1_BIT);

    const VkVertexInputBindingDescription ui_binding{0, sizeof(UiGpuVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription ui_attributes[]{
        {0, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<std::uint32_t>(offsetof(UiGpuVertex, x))},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<std::uint32_t>(offsetof(UiGpuVertex, u))},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<std::uint32_t>(offsetof(UiGpuVertex, r))}};
    VkPipelineVertexInputStateCreateInfo ui_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    ui_input.vertexBindingDescriptionCount = 1;
    ui_input.pVertexBindingDescriptions = &ui_binding;
    ui_input.vertexAttributeDescriptionCount = 3;
    ui_input.pVertexAttributeDescriptions = ui_attributes;
    ui_pipeline_ = create_pipeline(ui_vertex, ui_fragment, ui_layout_, true, &ui_input, swapchain_format_, VK_SAMPLE_COUNT_1_BIT);

    vkDestroyShaderModule(device_, ui_fragment, nullptr);
    vkDestroyShaderModule(device_, ui_vertex, nullptr);
    vkDestroyShaderModule(device_, living_fragment, nullptr);
    vkDestroyShaderModule(device_, living_vertex, nullptr);
    vkDestroyShaderModule(device_, political, nullptr);
    vkDestroyShaderModule(device_, ocean, nullptr);
    vkDestroyShaderModule(device_, terrain, nullptr);
    vkDestroyShaderModule(device_, fullscreen, nullptr);

    const std::array<LivingVertex, 3> living_vertices{{
        {-0.45f, -0.25f, 0.0f},
        {-0.20f, -0.25f, 0.0f},
        {-0.325f, 0.08f, 0.0f}}};
    const std::array<float, 16> identity{{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f}};
    create_host_buffer(sizeof(living_vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                       living_vertices.data(), living_vertices_, living_vertices_memory_);
    create_host_buffer(sizeof(identity), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                       identity.data(), living_instance_, living_instance_memory_);

    const std::array<UiGpuVertex, 6> ui_vertices{{
        {28.0f, 28.0f, 0.0f, 0.0f, 0.06f, 0.07f, 0.09f, 0.88f},
        {380.0f, 28.0f, 1.0f, 0.0f, 0.06f, 0.07f, 0.09f, 0.88f},
        {380.0f, 96.0f, 1.0f, 1.0f, 0.06f, 0.07f, 0.09f, 0.88f},
        {28.0f, 28.0f, 0.0f, 0.0f, 0.06f, 0.07f, 0.09f, 0.88f},
        {380.0f, 96.0f, 1.0f, 1.0f, 0.06f, 0.07f, 0.09f, 0.88f},
        {28.0f, 96.0f, 0.0f, 1.0f, 0.06f, 0.07f, 0.09f, 0.88f}}};
    create_host_buffer(sizeof(ui_vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                       ui_vertices.data(), ui_vertices_, ui_vertices_memory_);

    live_renderer_enabled_ = true;
}

void VulkanDesktopBackend::destroy_live_validation_renderer() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    for (auto& frame : ui_frame_buffers_) {
        if (frame.vertex_mapped != nullptr && frame.vertex_memory != VK_NULL_HANDLE) {
            vkUnmapMemory(device_, frame.vertex_memory);
            frame.vertex_mapped = nullptr;
        }
        if (frame.vertex_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, frame.vertex_buffer, nullptr);
            frame.vertex_buffer = VK_NULL_HANDLE;
        }
        if (frame.vertex_memory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, frame.vertex_memory, nullptr);
            frame.vertex_memory = VK_NULL_HANDLE;
        }
        frame.vertex_capacity = 0;
        if (frame.index_mapped != nullptr && frame.index_memory != VK_NULL_HANDLE) {
            vkUnmapMemory(device_, frame.index_memory);
            frame.index_mapped = nullptr;
        }
        if (frame.index_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, frame.index_buffer, nullptr);
            frame.index_buffer = VK_NULL_HANDLE;
        }
        if (frame.index_memory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, frame.index_memory, nullptr);
            frame.index_memory = VK_NULL_HANDLE;
        }
        frame.index_capacity = 0;
    }
    if (ui_vertices_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, ui_vertices_, nullptr);
        ui_vertices_ = VK_NULL_HANDLE;
    }
    if (ui_vertices_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, ui_vertices_memory_, nullptr);
        ui_vertices_memory_ = VK_NULL_HANDLE;
    }
    if (living_instance_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, living_instance_, nullptr);
        living_instance_ = VK_NULL_HANDLE;
    }
    if (living_instance_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, living_instance_memory_, nullptr);
        living_instance_memory_ = VK_NULL_HANDLE;
    }
    if (living_vertices_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, living_vertices_, nullptr);
        living_vertices_ = VK_NULL_HANDLE;
    }
    if (living_vertices_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, living_vertices_memory_, nullptr);
        living_vertices_memory_ = VK_NULL_HANDLE;
    }

    const std::array<VkPipeline*, 5> pipelines{{
        &ui_pipeline_, &living_pipeline_, &political_pipeline_, &ocean_pipeline_, &terrain_pipeline_}};
    for (auto* pipeline : pipelines) {
        if (*pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, *pipeline, nullptr);
            *pipeline = VK_NULL_HANDLE;
        }
    }
    if (ui_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, ui_layout_, nullptr);
        ui_layout_ = VK_NULL_HANDLE;
    }
    if (political_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, political_layout_, nullptr);
        political_layout_ = VK_NULL_HANDLE;
    }
    if (fullscreen_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, fullscreen_layout_, nullptr);
        fullscreen_layout_ = VK_NULL_HANDLE;
    }
    if (tonemap_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, tonemap_layout_, nullptr);
        tonemap_layout_ = VK_NULL_HANDLE;
    }
    if (hdr_descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, hdr_descriptor_pool_, nullptr);
        hdr_descriptor_pool_ = VK_NULL_HANDLE;
        hdr_descriptor_set_ = VK_NULL_HANDLE;
    }
    if (hdr_descriptor_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, hdr_descriptor_layout_, nullptr);
        hdr_descriptor_layout_ = VK_NULL_HANDLE;
    }
    if (hdr_sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, hdr_sampler_, nullptr);
        hdr_sampler_ = VK_NULL_HANDLE;
    }
    live_renderer_enabled_ = false;
}

void VulkanDesktopBackend::submit_ui(const UiDrawList& ui) {
    ui_staging_vertices_.clear();
    ui_staging_indices_.clear();
    ui_staging_batches_.clear();

    const auto source_vertices = ui.vertices();
    const auto source_indices = ui.indices();
    ui_staging_vertices_.reserve(source_vertices.size());
    ui_staging_indices_.reserve(source_indices.size());
    ui_staging_batches_.reserve(ui.batches().size() + ui.text_runs().size());
    for (const auto& vertex : source_vertices) {
        ui_staging_vertices_.push_back(ui_gpu_vertex(vertex));
    }

    // Validate each batch at the content boundary.  A malformed mod/UI script
    // must not be able to feed an out-of-range index to the GPU.
    for (const auto& batch : ui.batches()) {
        if (batch.first_index >= source_indices.size()) continue;
        const auto end = std::min<std::size_t>(source_indices.size(),
                                               static_cast<std::size_t>(batch.first_index) + batch.index_count);
        const auto first = static_cast<std::uint32_t>(ui_staging_indices_.size());
        for (std::size_t index = batch.first_index; index < end; ++index) {
            const auto value = source_indices[index];
            if (value < source_vertices.size()) ui_staging_indices_.push_back(value);
        }
        const auto count = static_cast<std::uint32_t>(ui_staging_indices_.size()) - first;
        if (count != 0u) {
            ui_staging_batches_.push_back(UiGpuBatch{first, count, ui_scissor(batch.scissor, extent_)});
        }
    }

    // Text runs are intentionally kept out of UiDrawList's geometry arrays so
    // other renderers can choose a real font atlas.  The desktop fallback uses
    // a tiny deterministic 5x7 bitmap font, which keeps the validation shell
    // readable without shipping a proprietary font or texture dependency.
    for (const auto& run : ui.text_runs()) {
        if (!std::isfinite(run.x) || !std::isfinite(run.y) || !std::isfinite(run.size) || run.size <= 0.0f) continue;
        const auto first = static_cast<std::uint32_t>(ui_staging_indices_.size());
        const float cell = std::max(1.0f, run.size / 7.0f);
        float x = run.x;
        float y = run.y;
        const auto color = ui_gpu_vertex(UiVertex{0.0f, 0.0f, 0.0f, 0.0f, run.rgba});
        for (const unsigned char byte : run.utf8) {
            if (byte == '\n') {
                x = run.x;
                y += cell * 9.0f;
                continue;
            }
            if (byte >= 0x80u) {
                x += cell * 6.0f;
                continue;
            }
            const auto rows = glyph_rows(static_cast<char>(byte));
            for (std::uint32_t row = 0; row < rows.size(); ++row) {
                for (std::uint32_t column = 0; column < 5u; ++column) {
                    if ((rows[row] & static_cast<std::uint8_t>(1u << (4u - column))) == 0u) continue;
                    const auto base = static_cast<std::uint32_t>(ui_staging_vertices_.size());
                    const float px = x + static_cast<float>(column) * cell;
                    const float py = y + static_cast<float>(row) * cell;
                    ui_staging_vertices_.push_back(UiGpuVertex{px, py, 0.0f, 0.0f, color.r, color.g, color.b, color.a});
                    ui_staging_vertices_.push_back(UiGpuVertex{px + cell, py, 1.0f, 0.0f, color.r, color.g, color.b, color.a});
                    ui_staging_vertices_.push_back(UiGpuVertex{px + cell, py + cell, 1.0f, 1.0f, color.r, color.g, color.b, color.a});
                    ui_staging_vertices_.push_back(UiGpuVertex{px, py + cell, 0.0f, 1.0f, color.r, color.g, color.b, color.a});
                    ui_staging_indices_.insert(ui_staging_indices_.end(), {base + 0u, base + 1u, base + 2u,
                                                                             base + 0u, base + 2u, base + 3u});
                }
            }
            x += cell * 6.0f;
        }
        const auto count = static_cast<std::uint32_t>(ui_staging_indices_.size()) - first;
        if (count != 0u) ui_staging_batches_.push_back(UiGpuBatch{first, count, ui_scissor(run.scissor, extent_)});
    }

    if (!ui_staging_vertices_.empty() && !ui_staging_indices_.empty() && device_ != VK_NULL_HANDLE) {
        ensure_ui_frame_buffers();
    }
}

void VulkanDesktopBackend::ensure_ui_frame_buffers() {
    if (device_ == VK_NULL_HANDLE) return;
    const auto vertex_bytes = std::max<std::size_t>(sizeof(UiGpuVertex), ui_staging_vertices_.size() * sizeof(UiGpuVertex));
    const auto index_bytes = std::max<std::size_t>(sizeof(std::uint32_t), ui_staging_indices_.size() * sizeof(std::uint32_t));

    auto next_capacity = [](VkDeviceSize current, std::size_t required) {
        VkDeviceSize capacity = current == 0 ? 4096u : current;
        while (capacity < required) capacity = std::min<VkDeviceSize>(capacity * 2u, static_cast<VkDeviceSize>(std::numeric_limits<std::uint32_t>::max()));
        return capacity;
    };
    auto grow = [&](VkBuffer& buffer, VkDeviceMemory& memory, void*& mapped,
                    VkDeviceSize& capacity, VkDeviceSize required, VkBufferUsageFlags usage) {
        if (capacity >= required && buffer != VK_NULL_HANDLE && mapped != nullptr) return;
        if (mapped != nullptr && memory != VK_NULL_HANDLE) vkUnmapMemory(device_, memory);
        mapped = nullptr;
        if (buffer != VK_NULL_HANDLE) vkDestroyBuffer(device_, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        if (memory != VK_NULL_HANDLE) vkFreeMemory(device_, memory, nullptr);
        memory = VK_NULL_HANDLE;
        capacity = next_capacity(capacity, static_cast<std::size_t>(required));

        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = capacity;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkcheck(vkCreateBuffer(device_, &info, nullptr, &buffer), "vkCreateBuffer(ui)");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_, buffer, &requirements);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = find_memory_type(requirements.memoryTypeBits,
                                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkcheck(vkAllocateMemory(device_, &allocation, nullptr, &memory), "vkAllocateMemory(ui)");
        vkcheck(vkBindBufferMemory(device_, buffer, memory, 0), "vkBindBufferMemory(ui)");
        vkcheck(vkMapMemory(device_, memory, 0, requirements.size, 0, &mapped), "vkMapMemory(ui)");
    };

    bool resize = false;
    for (const auto& frame : ui_frame_buffers_) {
        resize = resize || frame.vertex_capacity < vertex_bytes || frame.index_capacity < index_bytes;
    }
    if (resize) vkcheck(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle(ui resize)");
    for (auto& frame : ui_frame_buffers_) {
        grow(frame.vertex_buffer, frame.vertex_memory, frame.vertex_mapped, frame.vertex_capacity,
             static_cast<VkDeviceSize>(vertex_bytes), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        grow(frame.index_buffer, frame.index_memory, frame.index_mapped, frame.index_capacity,
             static_cast<VkDeviceSize>(index_bytes), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    }

    // submit_ui() runs before draw_frame().  Protect the persistently mapped
    // buffer for the current frame from the previous GPU submission; waiting
    // only in draw_frame() would leave a write-after-submit race when the
    // buffers have already reached their steady-state capacity.
    if (frames_[frame_index_].fence != VK_NULL_HANDLE)
        vkcheck(vkWaitForFences(device_, 1, &frames_[frame_index_].fence, VK_TRUE, UINT64_MAX),
                "vkWaitForFences(ui upload)");
    auto& frame = ui_frame_buffers_[frame_index_];
    std::memcpy(frame.vertex_mapped, ui_staging_vertices_.data(), ui_staging_vertices_.size() * sizeof(UiGpuVertex));
    std::memcpy(frame.index_mapped, ui_staging_indices_.data(), ui_staging_indices_.size() * sizeof(std::uint32_t));
}

void VulkanDesktopBackend::record_ui_draws(VkCommandBuffer command) const {
    if (!live_renderer_enabled_ || ui_pipeline_ == VK_NULL_HANDLE || ui_staging_indices_.empty()) return;
    const auto& frame = ui_frame_buffers_[frame_index_];
    if (frame.vertex_buffer == VK_NULL_HANDLE || frame.index_buffer == VK_NULL_HANDLE) return;
    VkViewport viewport{};
    viewport.width = static_cast<float>(extent_.width);
    viewport.height = static_cast<float>(extent_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D full{{0, 0}, extent_};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &full);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ui_pipeline_);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &frame.vertex_buffer, &offset);
    vkCmdBindIndexBuffer(command, frame.index_buffer, 0, VK_INDEX_TYPE_UINT32);
    const std::array<float, 2> inv_viewport{{
        1.0f / static_cast<float>(std::max(extent_.width, 1u)),
        1.0f / static_cast<float>(std::max(extent_.height, 1u))}};
    vkCmdPushConstants(command, ui_layout_, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(inv_viewport), inv_viewport.data());
    for (const auto& batch : ui_staging_batches_) {
        if (batch.index_count == 0u) continue;
        vkCmdSetScissor(command, 0, 1, &batch.scissor);
        vkCmdDrawIndexed(command, batch.index_count, 1, batch.first_index, 0, 0);
    }
}

void VulkanDesktopBackend::record_live_validation_draws(VkCommandBuffer command) const {
    if (!live_renderer_enabled_) {
        return;
    }
    VkViewport viewport{};
    viewport.width = static_cast<float>(extent_.width);
    viewport.height = static_cast<float>(extent_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{};
    scissor.extent = extent_;
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);

    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, terrain_pipeline_);
    vkCmdDraw(command, 3, 1, 0, 0);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ocean_pipeline_);
    vkCmdDraw(command, 3, 1, 0, 0);

    const std::array<float, 4> political_color{{0.34f, 0.18f, 0.12f, 0.22f}};
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, political_pipeline_);
    vkCmdPushConstants(command, political_layout_, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(political_color), political_color.data());
    vkCmdDraw(command, 3, 1, 0, 0);

    const VkBuffer living_buffers[]{living_vertices_, living_instance_};
    const VkDeviceSize living_offsets[]{0, 0};
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, living_pipeline_);
    vkCmdBindVertexBuffers(command, 0, 2, living_buffers, living_offsets);
    vkCmdDraw(command, 3, 1, 0, 0);

    // Keep a small deterministic panel for the renderer validation probe when
    // no application UI was submitted.  Real frames use the indexed draw-list
    // path below, which is replaced every frame by submit_ui().
    if (ui_staging_indices_.empty()) {
        const VkDeviceSize ui_offset = 0;
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ui_pipeline_);
        vkCmdBindVertexBuffers(command, 0, 1, &ui_vertices_, &ui_offset);
        const std::array<float, 2> inv_viewport{{
            1.0f / static_cast<float>(std::max(extent_.width, 1u)),
            1.0f / static_cast<float>(std::max(extent_.height, 1u))}};
        vkCmdPushConstants(command, ui_layout_, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(inv_viewport), inv_viewport.data());
        vkCmdDraw(command, 6, 1, 0, 0);
    }
}

void VulkanDesktopBackend::recreate_swapchain() {
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window_, &width, &height);
    if (width <= 0 || height <= 0) {
        return;
    }
    vkcheck(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle(recreate_swapchain)");
    destroy_live_validation_renderer();
    destroy_swapchain();
    create_swapchain();
    if (!shader_dir_.empty()) {
        create_live_validation_renderer();
    }
}

void VulkanDesktopBackend::draw_frame() {
    auto& frame = frames_[frame_index_];
    vkcheck(vkWaitForFences(device_, 1, &frame.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");

    std::uint32_t image_index = 0;
    const auto acquire = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, frame.acquired,
                                                VK_NULL_HANDLE, &image_index);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swapchain();
        return;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        vkcheck(acquire, "vkAcquireNextImageKHR");
    }

    vkcheck(vkResetFences(device_, 1, &frame.fence), "vkResetFences");
    vkcheck(vkResetCommandBuffer(frame.command, 0), "vkResetCommandBuffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkcheck(vkBeginCommandBuffer(frame.command, &begin), "vkBeginCommandBuffer");

    VkImageMemoryBarrier2 to_color{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    to_color.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    to_color.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    to_color.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    to_color.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_color.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    to_color.image = images_[image_index];
    to_color.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_color.subresourceRange.levelCount = 1;
    to_color.subresourceRange.layerCount = 1;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &to_color;
    vkCmdPipelineBarrier2(frame.command, &dependency);

    VkClearValue clear{};
    clear.color = {{0.025f, 0.055f, 0.085f, 1.0f}};
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = views_[image_index];
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue = clear;
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent = extent_;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    vkCmdBeginRendering(frame.command, &rendering);
    record_live_validation_draws(frame.command);
    record_ui_draws(frame.command);
    vkCmdEndRendering(frame.command);

    VkImageMemoryBarrier2 to_present{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    to_present.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    to_present.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    to_present.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    to_present.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    to_present.image = images_[image_index];
    to_present.subresourceRange = to_color.subresourceRange;
    dependency.pImageMemoryBarriers = &to_present;
    vkCmdPipelineBarrier2(frame.command, &dependency);
    vkcheck(vkEndCommandBuffer(frame.command), "vkEndCommandBuffer");

    VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = frame.acquired;
    wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    command.commandBuffer = frame.command;
    VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal.semaphore = image_rendered_[image_index];
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &wait;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal;
    vkcheck(vkQueueSubmit2(graphics_queue_, 1, &submit, frame.fence), "vkQueueSubmit2");

    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &image_rendered_[image_index];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &image_index;
    const auto present_result = vkQueuePresentKHR(present_queue_, &present);
    if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR) {
        recreate_swapchain();
    } else {
        vkcheck(present_result, "vkQueuePresentKHR");
    }

    frame_index_ = (frame_index_ + 1u) % frames_in_flight;
    ++frames_presented_;
}

void VulkanDesktopBackend::wait_idle() {
    if (device_ != VK_NULL_HANDLE) {
        vkcheck(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
    }
}

void VulkanDesktopBackend::write_report(const std::filesystem::path& path) const {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("cannot create GPU report");
    }
    output << "core_gpu_validation=1\n"
           << "device=" << properties_.deviceName << '\n'
           << "vendor_id=" << properties_.vendorID << '\n'
           << "device_id=" << properties_.deviceID << '\n'
           << "api_version=" << VK_API_VERSION_MAJOR(properties_.apiVersion) << '.'
           << VK_API_VERSION_MINOR(properties_.apiVersion) << '.'
           << VK_API_VERSION_PATCH(properties_.apiVersion) << '\n'
           << "driver_version=" << properties_.driverVersion << '\n'
           << "frames_presented=" << frames_presented_ << '\n'
           << "validation_enabled=" << (validation_enabled_ ? 1 : 0) << '\n'
           << "validation_errors=" << validation_errors() << '\n'
           << "validation_warnings=" << validation_warnings() << '\n'
           << "dynamic_rendering=" << (supports_dynamic_rendering_ ? 1 : 0) << '\n'
           << "synchronization2=" << (supports_synchronization2_ ? 1 : 0) << '\n'
           << "timeline_semaphore=" << (supports_timeline_ ? 1 : 0) << '\n'
           << "buffer_device_address=" << (supports_bda_ ? 1 : 0) << '\n'
           << "descriptor_indexing=" << (supports_descriptor_indexing_ ? 1 : 0) << '\n'
           << "draw_indirect_count=" << (supports_draw_indirect_count_ ? 1 : 0) << '\n'
           << "shader_draw_parameters=" << (supports_shader_draw_parameters_ ? 1 : 0) << '\n'
           << "device_local_bytes=" << device_local_bytes_ << '\n'
           << "max_sampled_images=" << max_sampled_images_ << '\n'
           << "swapchain_format=" << static_cast<std::uint32_t>(swapchain_format_) << '\n'
           << "swapchain_width=" << extent_.width << '\n'
           << "swapchain_height=" << extent_.height << '\n'
           << "live_renderer_enabled=" << (live_renderer_enabled_ ? 1 : 0) << '\n'
           << "terrain_pipeline=" << (terrain_pipeline_ != VK_NULL_HANDLE ? 1 : 0) << '\n'
           << "ocean_pipeline=" << (ocean_pipeline_ != VK_NULL_HANDLE ? 1 : 0) << '\n'
           << "political_pipeline=" << (political_pipeline_ != VK_NULL_HANDLE ? 1 : 0) << '\n'
           << "living_pipeline=" << (living_pipeline_ != VK_NULL_HANDLE ? 1 : 0) << '\n'
           << "ui_pipeline=" << (ui_pipeline_ != VK_NULL_HANDLE ? 1 : 0) << '\n';
}

} // namespace core
