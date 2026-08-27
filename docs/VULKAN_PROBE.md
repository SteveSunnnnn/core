# Core Vulkan Loader Probe

Core 0.4 includes a tiny SDK-independent Vulkan bootstrap probe. It exists so CI/headless machines
can validate the first part of the Vulkan path even when SDL/window-system development packages are
unavailable.

## What it does

1. Dynamically loads `libvulkan.so.1` on Linux or `vulkan-1.dll` on Windows.
2. Resolves `vkGetInstanceProcAddr`.
3. Calls `vkEnumerateInstanceVersion` when available.
4. Calls `vkCreateInstance` with no WSI extensions.
5. If instance creation succeeds, calls `vkEnumeratePhysicalDevices`.
6. Destroys the instance.

It intentionally does not replace official Vulkan headers in the production renderer. The probe
uses only the minimal ABI subset needed for loader smoke testing. The real backend remains expected
to compile against the official Vulkan SDK/headers.

## Current container result

```text
loader: libvulkan.so.1
loader_found: yes
api: 1.4.309
instance_created: no
physical_devices: 0
vkCreateInstance result: -9
```

`-9` is `VK_ERROR_INCOMPATIBLE_DRIVER`, consistent with this container exposing the loader library
but no usable Vulkan ICD/physical device. This proves dynamic-loader reachability and call ABI up to
instance creation; it does **not** validate device creation, WSI or swapchain rendering.
