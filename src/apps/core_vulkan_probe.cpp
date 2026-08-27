#include "core/render/vulkan/VulkanProbe.hpp"

#include <iostream>

int main() {
    const auto result = core::probe_vulkan_loader();
    std::cout << "Core Vulkan loader probe\n"
              << "loader: " << result.loader_name << "\n"
              << "loader_found: " << (result.loader_found ? "yes" : "no") << "\n"
              << "api: " << result.api_major() << '.' << result.api_minor() << '.' << result.api_patch() << "\n"
              << "instance_created: " << (result.instance_created ? "yes" : "no") << "\n"
              << "physical_devices: " << result.physical_device_count << "\n"
              << "vkCreateInstance result: " << result.create_instance_result << "\n"
              << result.message << '\n';
    return result.loader_found ? 0 : 2;
}
